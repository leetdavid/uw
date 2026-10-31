// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TERMINAL_CLOSE_GUARD_H_
#define UW_BROWSER_TERMINAL_CLOSE_GUARD_H_

#include <vector>

#include "base/functional/callback_forward.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

namespace content {
class WebContents;
}

namespace uw {

// Asks the user before a close stops a program running in a terminal tab.
// Chromium's tab strip, window close, and quit ask the window's guard, and
// cannot depend on the views that show the question. Each Hold* is false
// when nothing needs asking, and the close goes ahead at once. True holds
// the close until the user answers. See
// docs/plans/terminal-tab.md#busy-terminal-warning.
class CloseGuard {
 public:
  DECLARE_USER_DATA(CloseGuard);

  CloseGuard(const CloseGuard&) = delete;
  CloseGuard& operator=(const CloseGuard&) = delete;

  // `pages`, tabs of this window the user closes. `done` gets the answer and
  // the pages still open.
  using TabsDone =
      base::OnceCallback<void(bool close,
                              std::vector<content::WebContents*> pages)>;
  virtual bool HoldTabs(const std::vector<content::WebContents*>& pages,
                        TabsDone done) = 0;
  // `task`, closed with its tabs. `close` runs if the user agrees.
  virtual bool HoldTask(const tab_groups::TabGroupId& task,
                        base::OnceClosure close) = 0;
  // This window, closed with the hidden windows of its other workspaces. It
  // closes the window again itself if the user agrees.
  virtual bool HoldWindow() = 0;
  // Quitting, which closes every window. `done` gets the answer.
  virtual bool HoldQuit(base::OnceCallback<void(bool quit)> done) = 0;

 protected:
  explicit CloseGuard(ui::UnownedUserDataHost& host);
  virtual ~CloseGuard();

 private:
  ui::ScopedUnownedUserData<CloseGuard> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_CLOSE_GUARD_H_
