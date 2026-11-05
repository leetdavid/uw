// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/mini_player_placement.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"  // nogncheck
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/base_window.h"
#include "uw/browser/ui/views/mini_player/app_activity.h"
#include "uw/browser/ui/views/mini_player/dock_cover.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

namespace uw {
namespace {

// Long enough for a workspace switch to hide one window and show the next.
constexpr base::TimeDelta kSettle = base::Milliseconds(150);

// Every placement, so that docking by hand reaches them all.
std::set<MiniPlayerPlacement*>& All() {
  static base::NoDestructor<std::set<MiniPlayerPlacement*>> all;
  return *all;
}

base::RepeatingClosureList& HomeObservers() {
  static base::NoDestructor<base::RepeatingClosureList> observers;
  return *observers;
}

// Tells the observers once the task that changed a placement ends, when its
// owner is in place.
void NotifyHomesSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([] { HomeObservers().Notify(); }));
}

// A site's own picture-in-picture window is a browser window too, with no
// sidebar: the front window is the last active normal one.
BrowserWindowInterface* FrontWindow() {
  BrowserWindowInterface* front = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&front](BrowserWindowInterface* window) {
        if (window->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
          return true;
        }
        front = window;
        return false;
      },
      BrowserCollection::Order::kActivation);
  return front;
}

// The window of `contents` while it has a sidebar to dock in, else the window
// in front.
BrowserWindowInterface* WindowOf(content::WebContents* contents) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  return MiniPlayerDock::From(window) ? window : FrontWindow();
}

// False while the sidebar of `window` is hidden.
bool SidebarShows(BrowserWindowInterface* window) {
  const auto* sidebar = tabs::VerticalTabStripStateController::From(window);
  return !sidebar ||
         (sidebar->ShouldDisplayVerticalTabs() && !sidebar->IsCollapsed());
}

}  // namespace

MiniPlayerPlacement::MiniPlayerPlacement(
    views::Widget* floating_window,
    base::RepeatingCallback<content::WebContents*()> tab,
    PlaceCallback place)
    : floating_window_(floating_window),
      tab_(std::move(tab)),
      place_(std::move(place)) {
  All().insert(this);
  NotifyHomesSoon();
  browsers_.Observe(GlobalBrowserCollection::GetInstance());
  app_activity_ = ObserveAppActivity(base::BindRepeating(
      &MiniPlayerPlacement::ScheduleUpdate, base::Unretained(this)));
  if (PrefService* local_state = g_browser_process->local_state()) {
    prefs_.Init(local_state);
    for (const char* pref :
         {kMiniPlayerModePref, kMiniPlayerFloatInBackgroundPref}) {
      prefs_.Add(pref, base::BindRepeating(&MiniPlayerPlacement::Update,
                                           base::Unretained(this)));
    }
  }
}

MiniPlayerPlacement::~MiniPlayerPlacement() {
  All().erase(this);
  NotifyHomesSoon();
}

void MiniPlayerPlacement::Update() {
  settle_.Stop();
  Place(Choose());
}

void MiniPlayerPlacement::UpdateNew() {
  settle_.Stop();
  Place(Choose(/*just_opened=*/true));
}

void MiniPlayerPlacement::Place(MiniPlayerDock* dock) {
  placed_ = true;
  floats_ = !dock;
  place_.Run(dock);
}

void MiniPlayerPlacement::ScheduleUpdate() {
  settle_.Start(
      FROM_HERE, kSettle,
      base::BindOnce(&MiniPlayerPlacement::Update, base::Unretained(this)));
}

BrowserWindowInterface* MiniPlayerPlacement::home() {
  return home_ ? home_.get() : WindowOf(tab_.Run());
}

void MiniPlayerPlacement::SetHome(BrowserWindowInterface* window) {
  if (!window) {
    window = WindowOf(tab_.Run());
  }
  home_ = window ? window->GetWeakPtr() : nullptr;
  HomeObservers().Notify();
}

void MiniPlayerPlacement::MoveHere(BrowserWindowInterface* window) {
  popped_out_ = false;
  SetHome(window);
  Update();
}

// static
base::CallbackListSubscription MiniPlayerPlacement::ObserveHomes(
    base::RepeatingClosure callback) {
  return HomeObservers().Add(std::move(callback));
}

void MiniPlayerPlacement::PopOut() {
  popped_out_ = true;
  Update();
}

void MiniPlayerPlacement::DockByHand() {
  for (MiniPlayerPlacement* placement : All()) {
    placement->popped_out_ = false;
  }
  BringForward(home());
  // Placing one can end another, as a video closing a site's window.
  const std::set<MiniPlayerPlacement*> placements = All();
  for (MiniPlayerPlacement* placement : placements) {
    if (All().contains(placement)) {
      placement->Update();
    }
  }
}

// static
void MiniPlayerPlacement::BringForward(BrowserWindowInterface* window) {
  if (!window) {
    return;
  }
  ui::BaseWindow* base_window = window->GetWindow();
  if (base_window->IsMinimized()) {
    base_window->Restore();
  }
  base_window->Activate();
}

bool MiniPlayerPlacement::CanDock() {
  BrowserWindowInterface* window = home();
  MiniPlayerDock* dock = MiniPlayerDock::From(window);
  return MiniPlayerModeOf(g_browser_process->local_state()) ==
             MiniPlayerMode::kDock &&
         dock && dock->GetWidget() && !dock->GetWidget()->IsFullscreen() &&
         SidebarShows(window);
}

void MiniPlayerPlacement::OnBrowserActivated(BrowserWindowInterface* browser) {
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnBrowserDeactivated(
    BrowserWindowInterface* browser) {
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (browser == watched_) {
    Watch(nullptr, nullptr);
  }
  // Its tab's window takes the place of a home window that closes.
  if (home_.get() == browser) {
    home_.reset();
    HomeObservers().Notify();
  }
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnWidgetVisibilityChanged(views::Widget* widget,
                                                    bool visible) {
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnWidgetBoundsChanged(views::Widget* widget,
                                                const gfx::Rect& new_bounds) {
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnWidgetShowStateChanged(views::Widget* widget) {
  ScheduleUpdate();
}

void MiniPlayerPlacement::OnWidgetDestroying(views::Widget* widget) {
  Watch(nullptr, nullptr);
  ScheduleUpdate();
}

MiniPlayerDock* MiniPlayerPlacement::Choose(bool just_opened) {
  BrowserWindowInterface* window = home();
  MiniPlayerDock* dock = MiniPlayerDock::From(window);
  Watch(window, dock);
  PrefService* local_state = g_browser_process->local_state();
  if (MiniPlayerModeOf(local_state) != MiniPlayerMode::kDock || !dock ||
      !dock->GetWidget() || popped_out_) {
    return nullptr;
  }
  // With no uw window showing, as when uw is hidden, it floats.
  BrowserWindowInterface* front = FrontWindow();
  if (!front || !front->GetWindow()->IsVisible() ||
      front->GetWindow()->IsMinimized()) {
    return nullptr;
  }
  views::Widget* widget = dock->GetWidget();
  // Its home window in front with no sidebar to show it floats it. Out of
  // sight behind another uw window, it keeps playing there.
  if (widget->IsFullscreen() || !SidebarShows(window)) {
    return window == front ? nullptr : dock;
  }
  if (IsAppActive()) {
    // A click on the floating mini player brings uw to the front, but it is
    // not coming back to a window.
    return window == front && !current_ && !just_opened &&
                   floating_window_->IsActive()
               ? nullptr
               : dock;
  }
  // With another app in front, a home window that was out of sight keeps it
  // there.
  if (!local_state->GetBoolean(kMiniPlayerFloatInBackgroundPref) ||
      !widget->IsVisible() || widget->IsMinimized()) {
    return dock;
  }
  // Otherwise it floats only once that app's windows cover its dock, and then
  // stays floating until uw comes back. One that opens now looks at once at
  // where it would dock.
  if (!placed_) {
    return CoveredPart(widget, dock->PlaceInScreen()) >= kHiddenPart ? nullptr
                                                                     : dock;
  }
  return floats_ || dock->covered() ? nullptr : dock;
}

void MiniPlayerPlacement::Watch(BrowserWindowInterface* home,
                                MiniPlayerDock* dock) {
  if (home == watched_) {
    return;
  }
  watched_ = home;
  window_.Reset();
  collapse_ = {};
  cover_ = {};
  if (dock && dock->GetWidget()) {
    window_.Observe(dock->GetWidget());
  }
  if (dock) {
    cover_ = dock->ObserveCover(base::BindRepeating(
        &MiniPlayerPlacement::ScheduleUpdate, base::Unretained(this)));
  }
  auto* sidebar =
      home ? tabs::VerticalTabStripStateController::From(home) : nullptr;
  if (sidebar) {
    collapse_ = sidebar->RegisterOnCollapseChanged(
        base::IgnoreArgs<tabs::VerticalTabStripCollapseState>(
            base::BindRepeating(&MiniPlayerPlacement::ScheduleUpdate,
                                base::Unretained(this))));
  }
}

}  // namespace uw
