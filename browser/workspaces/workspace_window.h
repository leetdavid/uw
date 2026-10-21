// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

class BrowserWindowInterface;
class Profile;

namespace uw {

// Owned by EmbedderBrowserWindowFeatures for each normal window that is not
// private. Says which workspace the window shows, and groups it with its
// partners: a window for each other workspace the user switched to from it,
// in the same place, of which one shows at a time. Switching shows the
// partner and hides this window, per ADR 0009. The workspace, the group, and
// whether the window is hidden are saved with the session, so a restart or
// Cmd+Shift+T brings the whole group back.
class WorkspaceWindow {
 public:
  DECLARE_USER_DATA(WorkspaceWindow);
  static constexpr char kWorkspaceKey[] = "uw.workspace";
  static constexpr char kGroupKey[] = "uw.workspace_group";
  static constexpr char kHiddenKey[] = "uw.workspace_hidden";

  explicit WorkspaceWindow(BrowserWindowInterface* window);
  WorkspaceWindow(const WorkspaceWindow&) = delete;
  WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;
  ~WorkspaceWindow();

  static WorkspaceWindow* From(BrowserWindowInterface* window);

  BrowserWindowInterface* window() { return window_; }
  const std::string& workspace() const { return workspace_; }
  bool hidden() const { return hidden_; }
  // The other windows of this window's group.
  std::vector<WorkspaceWindow*> Partners() const;

  // Shows workspace `id` where this window is. Its partner for `id` opens on
  // the first switch, once the profile has loaded.
  void Switch(std::string_view id);

  // A window that session restore or Cmd+Shift+T brings back rejoins its
  // workspace and group. Cmd+Shift+T brings back the rest of the group too.
  static void Restore(BrowserWindowInterface* window,
                      const std::map<std::string, std::string>& extra_data);
  // What Cmd+Shift+T needs to bring `window` back with its group.
  static void AddRestoreData(BrowserWindowInterface* window,
                             std::map<std::string, std::string>& extra_data);
  // Whether `window` is a hidden partner of the shown window `closing`, so
  // the session keeps both when they close as the last windows.
  static bool ClosesWith(BrowserWindowInterface* window, SessionID closing);
  // Asked before `window` shows or activates. A hidden window stays hidden
  // while it is being restored. Otherwise showing it shows its workspace in
  // place of the one its group shows, and it shows itself.
  static bool WillShow(BrowserWindowInterface* window);

 private:
  // Puts this window where the group's shown window is, shows it, and hides
  // that one.
  void Reveal();
  void OpenPartner(const std::string& id, Profile* profile);
  void SaveSession();
  void OnClosed(BrowserWindowInterface* window);
  void RestorePartners();
  // Shows a window of each group no window shows, as when a group's shown
  // window did not come back with the session.
  static void RevealOrphans();

  const raw_ptr<BrowserWindowInterface> window_;
  std::string workspace_;
  std::string group_;
  bool hidden_ = false;
  base::CallbackListSubscription close_subscription_;
  ui::ScopedUnownedUserData<WorkspaceWindow> user_data_;
  base::WeakPtrFactory<WorkspaceWindow> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_
