// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class Profile;

namespace tabs {
class TabInterface;
}

namespace uw {

// Owned by EmbedderBrowserWindowFeatures for each normal window that is not
// private, from before the window's views are built. Says which workspace the
// window shows, and groups it with its partners: a window for each other
// workspace the user switched to from it, in the same place, of which one shows
// at a time. Switching shows the partner and hides this window, per ADR 0009.
// The workspace, the group, and whether the window is hidden are saved with the
// session, so a restart or Cmd+Shift+T brings the whole group back.
class WorkspaceWindow : public views::WidgetObserver {
 public:
  DECLARE_USER_DATA(WorkspaceWindow);
  static constexpr char kWorkspaceKey[] = "uw.workspace";
  static constexpr char kGroupKey[] = "uw.workspace_group";
  static constexpr char kHiddenKey[] = "uw.workspace_hidden";

  explicit WorkspaceWindow(BrowserWindowInterface* window);
  WorkspaceWindow(const WorkspaceWindow&) = delete;
  WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;
  ~WorkspaceWindow() override;

  static WorkspaceWindow* From(BrowserWindowInterface* window);
  // Whether `window` is a workspace window kept hidden behind its partner.
  // Its restored tabs load once it shows.
  static bool IsHidden(BrowserWindowInterface* window);

  // Called once the window is built and its session tracks it.
  void Start();
  // Tells `changed` when a restore gives the window another workspace.
  base::CallbackListSubscription ObserveChanges(base::RepeatingClosure changed);
  // Tells `shown` each time a hidden window becomes the one its group shows,
  // once its partner is hidden.
  static base::CallbackListSubscription ObserveReveals(
      base::RepeatingCallback<void(BrowserWindowInterface*)> shown);

  BrowserWindowInterface* window() { return window_; }
  const std::string& workspace() const { return workspace_; }
  bool hidden() const { return hidden_; }
  // The other windows of this window's group.
  std::vector<WorkspaceWindow*> Partners() const;

  using WindowCallback = base::OnceCallback<void(BrowserWindowInterface*)>;

  // Shows workspace `id` where this window is, then gives `shown` the window
  // that shows it. Its partner for `id` opens on the first switch, once the
  // profile has loaded.
  void Switch(std::string_view id, WindowCallback shown = {});
  // Gives `found` this group's window for workspace `id`, opened hidden if
  // there is none yet.
  void WithPartner(std::string_view id, WindowCallback found);

  // Adds a workspace on `profile`, or on a new profile named `new_profile`
  // when `profile` is empty, and switches `window` to it.
  static void Create(BrowserWindowInterface* window,
                     std::string name,
                     std::string icon,
                     const base::FilePath& profile,
                     const std::u16string& new_profile,
                     WindowCallback shown);
  // Removes workspace `id` and closes its windows, each after it switches
  // to the workspace before it. Cmd+Shift+T does not bring them back, nor
  // the workspace's windows that closed before. With `with_profile`, the
  // profile goes too, once its windows are gone.
  static void Delete(std::string_view id, bool with_profile);
  // The tabs, besides pins, that deleting workspace `id` closes.
  static int CountTabs(std::string_view id);
  // Moves `tab` to the window for workspace `id` in its window's group.
  // Within a profile the page moves with its state. Across profiles its address
  // opens there and the tab closes here.
  static void MoveTab(tabs::TabInterface* tab, std::string_view id);

  // A window that session restore or Cmd+Shift+T brings back rejoins its
  // workspace and group. Cmd+Shift+T brings back the rest of the group too.
  // A window whose workspace is gone shows on its own, in its profile's first
  // workspace.
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

  // views::WidgetObserver:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // Puts this window where the group's shown window is, shows it, and hides
  // that one.
  void Reveal();
  // Hides the window's pages with it, so they neither load nor draw while it
  // is behind its partner. macOS counts a page in a window that is only
  // ordered out as visible.
  void SetHidden(bool hidden);
  // Asked once something past BrowserView, such as macOS's Window menu,
  // showed the window while it is hidden.
  void OnShownElsewhere();
  void OpenPartner(const std::string& id,
                   WindowCallback found,
                   Profile* profile);
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
  // Set when the window closes for a deleted workspace, which Cmd+Shift+T
  // does not bring back.
  bool forget_ = false;
  base::CallbackListSubscription close_subscription_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::RepeatingClosureList changes_;
  ui::ScopedUnownedUserData<WorkspaceWindow> user_data_;
  base::WeakPtrFactory<WorkspaceWindow> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_WINDOW_H_
