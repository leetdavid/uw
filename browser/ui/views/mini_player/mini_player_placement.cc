// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_placement.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"  // nogncheck
#include "components/prefs/pref_service.h"
#include "uw/browser/ui/views/mini_player/app_activity.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

namespace uw {
namespace {

// Long enough for a workspace switch to hide one window and show the next.
constexpr base::TimeDelta kSettle = base::Milliseconds(150);

}  // namespace

MiniPlayerPlacement::MiniPlayerPlacement(views::Widget* floating_window,
                                         PlaceCallback place)
    : floating_window_(floating_window), place_(std::move(place)) {
  browsers_.Observe(GlobalBrowserCollection::GetInstance());
  app_activity_ = ObserveAppActivity(base::BindRepeating(
      &MiniPlayerPlacement::ScheduleUpdate, base::Unretained(this)));
  if (PrefService* local_state = g_browser_process->local_state()) {
    prefs_.Init(local_state);
    for (const char* pref :
         {kMiniPlayerInSidebarPref, kMiniPlayerFloatInBackgroundPref}) {
      prefs_.Add(pref, base::BindRepeating(&MiniPlayerPlacement::Update,
                                           base::Unretained(this)));
    }
  }
}

MiniPlayerPlacement::~MiniPlayerPlacement() = default;

void MiniPlayerPlacement::Update() {
  settle_.Stop();
  place_.Run(Choose());
}

void MiniPlayerPlacement::ScheduleUpdate() {
  settle_.Start(
      FROM_HERE, kSettle,
      base::BindOnce(&MiniPlayerPlacement::Update, base::Unretained(this)));
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

MiniPlayerDock* MiniPlayerPlacement::Choose() {
  PrefService* local_state = g_browser_process->local_state();
  BrowserWindowInterface* front =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  MiniPlayerDock* dock = MiniPlayerDock::From(front);
  Watch(front, dock);
  if (!local_state || !local_state->GetBoolean(kMiniPlayerInSidebarPref) ||
      !dock || !dock->GetWidget()) {
    return nullptr;
  }
  views::Widget* window = dock->GetWidget();
  if (!window->IsVisible() || window->IsMinimized() || window->IsFullscreen()) {
    return nullptr;
  }
  const auto* sidebar = tabs::VerticalTabStripStateController::From(front);
  if (sidebar &&
      (!sidebar->ShouldDisplayVerticalTabs() || sidebar->IsCollapsed())) {
    return nullptr;
  }
  if (IsAppActive()) {
    // A click on the floating mini player brings uw to the front, but it is
    // not coming back to a window.
    return !current_ && floating_window_->IsActive() ? nullptr : dock;
  }
  return local_state->GetBoolean(kMiniPlayerFloatInBackgroundPref) ? nullptr
                                                                   : dock;
}

void MiniPlayerPlacement::Watch(BrowserWindowInterface* front,
                                MiniPlayerDock* dock) {
  if (front == watched_) {
    return;
  }
  watched_ = front;
  window_.Reset();
  collapse_ = {};
  if (dock && dock->GetWidget()) {
    window_.Observe(dock->GetWidget());
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
