// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "url/url_constants.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/little_window/little_window_key.h"
#include "uw/browser/little_window/little_window_mark.h"
#include "uw/browser/little_window/little_window_prefs.h"
#include "uw/browser/little_window/little_window_toolbar.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {

namespace {

// The first little window's size. Later ones open at the size the user last
// left one.
constexpr gfx::Size kFirstSize(900, 650);
// Each little window opens this far right of and below one already there.
constexpr int kOffset = 24;

std::optional<gfx::Size>& LastSize() {
  static std::optional<gfx::Size> size;
  return size;
}

// The window used last that shows workspace `id`: one in view, or else a
// window hidden behind its partner, which comes forward when shown.
BrowserWindowInterface* WindowOfWorkspace(std::string_view id) {
  BrowserWindowInterface* shown = nullptr;
  BrowserWindowInterface* hidden = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
        if (!workspace || workspace->workspace() != id ||
            IsRunWindow(window) || workspace->forgetting()) {
          return true;
        }
        if (!workspace->hidden()) {
          shown = window;
          return false;
        }
        if (!hidden) {
          hidden = window;
        }
        return true;
      });
  return shown ? shown : hidden;
}

struct Spot {
  raw_ptr<Profile> profile;
  std::string workspace;
};

// The profile and workspace of the window used last. Never a private one.
std::optional<Spot> SpotOfWindowUsedLast() {
  std::optional<Spot> found;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        if (window->GetProfile()->IsOffTheRecord() || IsRunWindow(window)) {
          return true;
        }
        if (IsLittleWindow(window)) {
          found = Spot{window->GetProfile(), LittleWindowWorkspace(window)};
          return false;
        }
        const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
        if (workspace && !workspace->hidden()) {
          found = Spot{window->GetProfile(), workspace->workspace()};
          return false;
        }
        return true;
      });
  if (found) {
    return found;
  }
  Profile* profile =
      g_browser_process->profile_manager()->GetLastUsedProfileIfLoaded();
  if (!profile) {
    return std::nullopt;
  }
  profile = profile->GetOriginalProfile();
  return Spot{profile, WorkspaceList::Get().FirstFor(profile).id};
}

// Where a new little window goes: in the middle of the screen with the
// pointer, at the size the user last left one, and clear of the corner of any
// little window already there.
gfx::Rect NewBounds() {
  display::Screen* screen = display::Screen::Get();
  const gfx::Rect area =
      screen->GetDisplayNearestPoint(screen->GetCursorScreenPoint())
          .work_area();
  gfx::Size size = LastSize().value_or(kFirstSize);
  size.SetToMin(area.size());
  gfx::Rect bounds(area.CenterPoint().x() - size.width() / 2,
                   area.CenterPoint().y() - size.height() / 2, size.width(),
                   size.height());
  for (bool taken = true; taken;) {
    taken = false;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (IsLittleWindow(window) && window->GetWindow() &&
              window->GetWindow()->GetBounds().origin() == bounds.origin()) {
            taken = true;
            return false;
          }
          return true;
        });
    if (taken) {
      bounds.Offset(kOffset, kOffset);
      if (!area.Contains(bounds)) {
        break;
      }
    }
  }
  bounds.AdjustToFit(area);
  return bounds;
}

BrowserWindowInterface* MakeLittleWindow(Profile* profile,
                                         std::string workspace,
                                         const gfx::Rect& bounds) {
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, *profile,
                                   /*from_user_gesture=*/true);
  params.initial_bounds = bounds;
  // A little window never comes back after uw quits.
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  BrowserWindowInterface* window = nullptr;
  {
    ScopedMakingLittleWindow making(std::move(workspace));
    window = CreateBrowserWindow(std::move(params));
  }
  if (window && window->GetWindow()) {
    internal::JoinActiveSpace(window->GetWindow()->GetNativeWindow());
  }
  return window;
}

// Loads `url`, or a blank page, in the new little window `window`'s only tab,
// and shows the window.
void Load(BrowserWindowInterface* window,
          const GURL& url,
          content::WebContents* opener) {
  NavigateParams params(window, url.is_empty() ? GURL(url::kAboutBlankURL) : url,
                        url.is_empty() ? ui::PAGE_TRANSITION_AUTO_TOPLEVEL
                                       : ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  params.source_contents = opener;
  Navigate(&params);
}

}  // namespace

BrowserWindowInterface* OpenLittleWindow(const GURL& url) {
  const std::optional<Spot> place = SpotOfWindowUsedLast();
  if (!place) {
    return nullptr;
  }
  BrowserWindowInterface* window =
      MakeLittleWindow(place->profile, place->workspace, NewBounds());
  if (!window) {
    return nullptr;
  }
  Load(window, url, nullptr);
  if (url.is_empty()) {
    CommandBar::Open(window);
  }
  return window;
}

BrowserWindowInterface* OpenLinkInLittleWindow(BrowserWindowInterface* from,
                                               const GURL& url,
                                               content::WebContents* opener) {
  Profile* profile = from->GetProfile();
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  std::string workspace = LittleWindowWorkspace(from);
  if (const WorkspaceWindow* shown = WorkspaceWindow::From(from)) {
    workspace = shown->workspace();
  }
  if (workspace.empty()) {
    workspace = WorkspaceList::Get().FirstFor(profile).id;
  }
  BrowserWindowInterface* window =
      MakeLittleWindow(profile, std::move(workspace), NewBounds());
  if (window) {
    Load(window, url, opener);
  }
  return window;
}

BrowserWindowInterface* WindowForLittleWindow(BrowserWindowInterface* window) {
  BrowserWindowInterface* found =
      WindowOfWorkspace(LittleWindowWorkspace(window));
  return found && found->GetProfile() == window->GetProfile() ? found
                                                              : nullptr;
}

DEFINE_USER_DATA(LittleWindow);

LittleWindow::LittleWindow(BrowserWindowInterface* window)
    : window_(window),
      last_active_(base::TimeTicks::Now()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  MarkLittleWindow(window, MakingLittleWindowWorkspace());
  active_ = window->RegisterDidBecomeActive(
      base::BindRepeating(&LittleWindow::OnActive, base::Unretained(this)));
  inactive_ = window->RegisterDidBecomeInactive(
      base::BindRepeating(&LittleWindow::OnInactive, base::Unretained(this)));
  unused_.Start(FROM_HERE, base::Minutes(1),
                base::BindRepeating(&LittleWindow::CloseIfUnused,
                                    base::Unretained(this)));
}

LittleWindow::~LittleWindow() {
  UnmarkLittleWindow(window_);
}

// static
LittleWindow* LittleWindow::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

void LittleWindow::AddViews(BrowserView* browser_view) {
  toolbar_ = std::make_unique<LittleWindowToolbar>(this, browser_view);
}

void LittleWindow::RemoveViews() {
  // The next little window opens at the size this one closes at.
  if (ui::BaseWindow* shown = window_->GetWindow()) {
    LastSize() = shown->GetRestoredBounds().size();
  }
  toolbar_.reset();
}

std::string LittleWindow::workspace() const {
  return LittleWindowWorkspace(window_);
}

void LittleWindow::MoveTo(std::string_view id) {
  const std::string to = id.empty() ? workspace() : std::string(id);
  if (!WorkspaceList::Get().Find(to) ||
      !window_->GetTabStripModel()->GetActiveTab()) {
    return;
  }
  if (BrowserWindowInterface* shown = WindowOfWorkspace(to)) {
    Deliver(to, shown);
    return;
  }
  WorkspaceWindow::OpenWindow(
      to, base::BindOnce(&LittleWindow::Deliver, weak_factory_.GetWeakPtr(),
                         to));
}

void LittleWindow::ShowWorkspaceMenu() {
  if (toolbar_) {
    toolbar_->ShowWorkspaceMenu();
  }
}

void LittleWindow::OnActive(BrowserWindowInterface* window) {
  last_active_ = base::TimeTicks::Now();
}

void LittleWindow::OnInactive(BrowserWindowInterface* window) {
  last_active_ = base::TimeTicks::Now();
}

void LittleWindow::CloseIfUnused() {
  const int minutes = g_browser_process->local_state()->GetInteger(
      kLittleWindowCloseAfterPref);
  const bool in_front = in_front_for_testing_.value_or(window_->IsActive());
  if (minutes <= 0 || in_front ||
      base::TimeTicks::Now() - last_active_ < base::Minutes(minutes)) {
    return;
  }
  tabs::TabInterface* tab = window_->GetTabStripModel()->GetActiveTab();
  if (!tab) {
    return;
  }
  content::WebContents* page = tab->GetContents();
  if (page->IsCurrentlyAudible() ||
      page->NeedToFireBeforeUnloadOrUnloadEvents()) {
    return;
  }
  window_->GetWindow()->Close();
}

void LittleWindow::Deliver(std::string id, BrowserWindowInterface* to) {
  TabStripModel* from = window_->GetTabStripModel();
  tabs::TabInterface* tab = from->GetActiveTab();
  if (!to || !tab) {
    return;
  }
  // The page lands where a link from another app does, and the organizer
  // files it from there.
  TabOrganizationController::LinksFromAnotherApp links;
  if (to->GetProfile() == window_->GetProfile()) {
    content::WebContents* page = tab->GetContents();
    TabStripModel* target = to->GetTabStripModel();
    // Taking the last tab closes the little window, on a later task.
    target->InsertDetachedTabAt(
        target->count(),
        from->DetachTabAtForInsertion(from->GetIndexOfTab(tab)),
        AddTabTypes::ADD_ACTIVE);
    TabOrganizationController::DidOpenTab(to, page, nullptr);
  } else {
    NavigateParams params(to, tab->GetContents()->GetLastCommittedURL(),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    window_->GetWindow()->Close();
  }
  to->GetWindow()->Show();
  to->GetWindow()->Activate();
}

}  // namespace uw
