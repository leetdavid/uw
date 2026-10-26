// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
#define UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
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

  bool IsShowing() const;
  // Null while the setting is off.
  content::WebContents* page() const;

 private:
  class Page;

  // views::WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  void OnSettingChanged();
  void Show();
  // Opens a row picked in the command bar: in a new tab where Cmd+T's blank
  // tab goes, or in the open tab it names. Then closes the command bar.
  void OpenPicked(const content::OpenURLParams& params);
  // Closes the command bar and asks `text` in a new chat, with the selected
  // tab handed to the agent.
  void AskUw(const std::string& text);
  // Closes the command bar and runs `text` in a new terminal.
  void RunInTerminal(const std::string& text);
  // Moves the tab just opened to the end of the task the command bar was
  // opened for, if any.
  void PlaceInTask();

  const raw_ptr<BrowserWindowInterface> window_;
  PrefChangeRegistrar setting_;
  std::unique_ptr<Page> contents_;
  base::WeakPtr<WebUIBubbleDialogView> bubble_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> panel_{this};
  std::optional<tab_groups::TabGroupId> task_;
  ui::ScopedUnownedUserData<CommandBar> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
