// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
#define UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class WebUIBubbleDialogView;

namespace content {
struct OpenURLParams;
class WebContents;
}  // namespace content

namespace uw {

// A uw command number for Cmd+Shift+P, which opens the command bar ready for a
// shell command. Above every IDC_ value and the chat's command.
inline constexpr int kShellCommandBarCommand = 56102;

// The command bar of one window, a panel over the page card. While the
// setting is on, its page stays loaded, so it shows at once. A click elsewhere
// closes it and keeps what it showed for the next time it opens.
class CommandBar : public views::WidgetObserver {
 public:
  DECLARE_USER_DATA(CommandBar);

  explicit CommandBar(BrowserWindowInterface* window);
  CommandBar(const CommandBar&) = delete;
  CommandBar& operator=(const CommandBar&) = delete;
  ~CommandBar() override;

  // A command the command bar lists, with its key as caps, such as
  // {"Option", "Cmd", "S"}.
  struct Command {
    int id;
    std::u16string name;
    std::vector<std::u16string> keys;
  };
  // The commands that can run now in a window.
  using CommandSource =
      base::RepeatingCallback<std::vector<Command>(BrowserWindowInterface*)>;
  static void SetCommandSource(CommandSource source);

  static CommandBar* From(BrowserWindowInterface* window);
  // Shows the command bar of `window`. False when the setting is off, and
  // Cmd+T opens a blank tab instead.
  static bool Open(BrowserWindowInterface* window);
  // Shows it ready for a shell command, as Cmd+Shift+P does.
  static bool OpenForShellCommand(BrowserWindowInterface* window);
  // Shows it for `task`, as the "+" on the task's row does. A page, chat, or
  // terminal it opens goes at the end of the task.
  static bool OpenForTask(BrowserWindowInterface* window,
                          const tab_groups::TabGroupId& task);
  // Shows it over `pane`, the new tab of a split view, as Split Right does.
  // A page, chat, or terminal it opens, or an open tab picked in it, takes
  // the place of `pane`.
  static bool OpenForPane(BrowserWindowInterface* window,
                          tabs::TabInterface* pane);

  // Shows it with only the tasks the selected tabs can move to, as
  // Option+Cmd+M does.
  static bool OpenToMoveTabs(BrowserWindowInterface* window);

  bool IsShowing() const;
  // Null while the setting is off.
  content::WebContents* page() const;

 private:
  class Page;

  // views::WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // Shows the command bar of `window` for `task`, over `pane`, or to move
  // tabs, each if given. With `reset_shell`, empties its page first, ready
  // for a shell command when true.
  static bool OpenWith(BrowserWindowInterface* window,
                       std::optional<tab_groups::TabGroupId> task,
                       tabs::TabInterface* pane,
                       bool moving,
                       std::optional<bool> reset_shell);
  void OnSettingChanged();
  void Show();
  // Opens a row picked in the command bar: in a new tab where Cmd+T's blank
  // tab goes, or in the open tab it names. Then closes the command bar.
  void OpenPicked(const content::OpenURLParams& params);
  // The selected tab when it shows a web page, or null.
  tabs::TabInterface* SelectedPage();
  // Closes the command bar and asks `text` in a new chat, with `page`, if
  // any, handed to the agent.
  void Ask(const std::string& text, tabs::TabInterface* page);
  // Closes the command bar and runs `text` in a new terminal.
  void RunInTerminal(const std::string& text);
  // Closes the command bar, then runs `command`, when the command bar lists
  // it.
  void RunCommand(int command);
  // Closes the command bar, then goes to `task`, or to the Scratchpad when
  // it is empty.
  void GoToTask(const std::string& task);
  // Closes the command bar, then moves the selected tabs, each with the tabs
  // under it, to the end of `task`, or of the Scratchpad when it is empty.
  void MoveToTask(const std::string& task);
  // Closes the command bar for a pick, an ask, or a command, so an empty
  // little window stays for what it opens.
  void CloseForPick();
  // Moves the tab just opened to the end of the task the command bar was
  // opened for, if any.
  void PlaceInTask();
  // Puts `tab`, just opened or picked, in the place of the pane the command
  // bar was opened for, if any.
  void PlaceInPane(tabs::TabInterface* tab);

  const raw_ptr<BrowserWindowInterface> window_;
  PrefChangeRegistrar setting_;
  base::CallbackListSubscription requests_;
  std::unique_ptr<Page> contents_;
  base::WeakPtr<WebUIBubbleDialogView> bubble_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> panel_{this};
  // True while a pick, an ask, or a terminal command closes the command bar.
  bool opening_ = false;
  // True while it lists the tasks the selected tabs can move to.
  bool moving_ = false;
  std::optional<tab_groups::TabGroupId> task_;
  tabs::TabHandle pane_;
  ui::ScopedUnownedUserData<CommandBar> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
