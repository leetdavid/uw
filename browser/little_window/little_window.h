// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"
#include "uw/browser/little_window/little_window_commands.h"

class BrowserView;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace uw {

class LittleWindowToolbar;

// Opens a little window on the workspace of the window used last, per
// docs/plans/little-window.md. Without `url` it opens blank, with the command
// bar over it.
BrowserWindowInterface* OpenLittleWindow(const GURL& url = GURL());

// Opens `url` in a little window on the workspace of `from`, as Option+Cmd-
// click and the link menu do. `opener` is the page the link was on.
BrowserWindowInterface* OpenLinkInLittleWindow(BrowserWindowInterface* from,
                                               const GURL& url,
                                               content::WebContents* opener);

// The window that the links of little window `window` open in, which a page
// moved out of it goes to: the window used last that shows its workspace.
// Null when no window shows it.
BrowserWindowInterface* WindowForLittleWindow(BrowserWindowInterface* window);

// What a little window is to uw: its workspace, which can change as its page
// moves, and Move to Workspace. Owned by EmbedderBrowserWindowFeatures for
// each little window, made while ScopedMakingLittleWindow lives.
class LittleWindow {
 public:
  DECLARE_USER_DATA(LittleWindow);

  explicit LittleWindow(BrowserWindowInterface* window);
  LittleWindow(const LittleWindow&) = delete;
  LittleWindow& operator=(const LittleWindow&) = delete;
  ~LittleWindow();

  static LittleWindow* From(BrowserWindowInterface* window);

  // Called once the window's views are built: adds Move to Workspace to its
  // toolbar. RemoveViews() takes it away before they go.
  void AddViews(BrowserView* browser_view);
  void RemoveViews();

  BrowserWindowInterface* window() { return window_; }
  std::string workspace() const;
  // When the window was last the one in front.
  base::TimeTicks last_active() const { return last_active_; }
  // Closes the window once it has not been in front for the time the Little
  // windows settings give, unless its page plays sound or would ask before it
  // is left. Checked each minute.
  void CloseIfUnused();
  void SetLastActiveForTesting(base::TimeTicks when) { last_active_ = when; }
  // Headless test windows stay active, so a test can say this one is not.
  void SetInFrontForTesting(bool in_front) { in_front_for_testing_ = in_front; }

  // Moves the page, as it is, to the top of the Scratchpad in the window used
  // last on workspace `id`, or on the little window's own workspace when `id`
  // is empty, and closes the little window. On another profile only the
  // address opens there.
  void MoveTo(std::string_view id);
  // Opens the menu of workspaces under Move to Workspace's arrow.
  void ShowWorkspaceMenu();

 private:
  void OnActive(BrowserWindowInterface* window);
  void OnInactive(BrowserWindowInterface* window);
  // Delivers the page to `to`, a window on workspace `id`, which may still be
  // opening.
  void Deliver(std::string id, BrowserWindowInterface* to);

  const raw_ptr<BrowserWindowInterface> window_;
  base::TimeTicks last_active_;
  base::CallbackListSubscription active_;
  base::CallbackListSubscription inactive_;
  base::RepeatingTimer unused_;
  std::optional<bool> in_front_for_testing_;
  std::unique_ptr<LittleWindowToolbar> toolbar_;
  ui::ScopedUnownedUserData<LittleWindow> user_data_;
  base::WeakPtrFactory<LittleWindow> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_H_
