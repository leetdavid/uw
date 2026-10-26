// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_placement.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"  // nogncheck
#include "components/prefs/pref_service.h"
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

}  // namespace

MiniPlayerPlacement::MiniPlayerPlacement(views::Widget* floating_window,
                                         PlaceCallback place)
    : floating_window_(floating_window), place_(std::move(place)) {
  All().insert(this);
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

void MiniPlayerPlacement::PopOut() {
  popped_out_ = true;
  Update();
}

// static
void MiniPlayerPlacement::DockByHand() {
  for (MiniPlayerPlacement* placement : All()) {
    placement->popped_out_ = false;
  }
  if (BrowserWindowInterface* front = FrontWindow()) {
    ui::BaseWindow* window = front->GetWindow();
    if (window->IsMinimized()) {
      window->Restore();
    }
    window->Activate();
  }
  // Placing one can end another, as a video closing a site's window.
  const std::set<MiniPlayerPlacement*> placements = All();
  for (MiniPlayerPlacement* placement : placements) {
    if (All().contains(placement)) {
      placement->Update();
    }
  }
}

bool MiniPlayerPlacement::CanDock() {
  return Dockable();
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

MiniPlayerDock* MiniPlayerPlacement::Dockable() {
  BrowserWindowInterface* front = FrontWindow();
  MiniPlayerDock* dock = MiniPlayerDock::From(front);
  Watch(front, dock);
  if (MiniPlayerModeOf(g_browser_process->local_state()) !=
          MiniPlayerMode::kDock ||
      !dock || !dock->GetWidget() || dock->GetWidget()->IsFullscreen()) {
    return nullptr;
  }
  const auto* sidebar = tabs::VerticalTabStripStateController::From(front);
  if (sidebar &&
      (!sidebar->ShouldDisplayVerticalTabs() || sidebar->IsCollapsed())) {
    return nullptr;
  }
  return dock;
}

MiniPlayerDock* MiniPlayerPlacement::Choose(bool just_opened) {
  MiniPlayerDock* dock = Dockable();
  if (!dock || popped_out_) {
    return nullptr;
  }
  views::Widget* window = dock->GetWidget();
  if (!window->IsVisible() || window->IsMinimized()) {
    return nullptr;
  }
  PrefService* local_state = g_browser_process->local_state();
  if (IsAppActive()) {
    // A click on the floating mini player brings uw to the front, but it is
    // not coming back to a window.
    return !current_ && !just_opened && floating_window_->IsActive() ? nullptr
                                                                     : dock;
  }
  if (!local_state->GetBoolean(kMiniPlayerFloatInBackgroundPref)) {
    return dock;
  }
  // With another app in front, it floats only once that app's windows cover
  // its dock, and then stays floating until uw comes back. One that opens now
  // looks at once at where it would dock.
  if (!placed_) {
    return CoveredPart(window, dock->PlaceInScreen()) >= kHiddenPart ? nullptr
                                                                     : dock;
  }
  return floats_ || dock->covered() ? nullptr : dock;
}

void MiniPlayerPlacement::Watch(BrowserWindowInterface* front,
                                MiniPlayerDock* dock) {
  if (front == watched_) {
    return;
  }
  watched_ = front;
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
      front ? tabs::VerticalTabStripStateController::From(front) : nullptr;
  if (sidebar) {
    collapse_ = sidebar->RegisterOnCollapseChanged(
        base::IgnoreArgs<tabs::VerticalTabStripCollapseState>(
            base::BindRepeating(&MiniPlayerPlacement::ScheduleUpdate,
                                base::Unretained(this))));
  }
}

}  // namespace uw
