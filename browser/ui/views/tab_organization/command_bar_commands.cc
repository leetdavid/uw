// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/command_bar_commands.h"

#include <optional>
#include <string>
#include <utility>

#include "chrome/app/chrome_command_ids.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/little_window/little_window_commands.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/tab_organization/key_card.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_commands.h"

namespace uw {
namespace {

struct UwCommand {
  int id;
  int name;
};

// In the order of the menu bar.
constexpr UwCommand kUwCommands[] = {
    {IDC_CREATE_NEW_TAB_GROUP, IDS_UW_NEW_TASK_MAC},
    {TabOrganizationController::kNewTask, IDS_UW_MOVE_TO_NEW_TASK_MAC},
    {kMoveToTask, IDS_UW_MOVE_TO_TASK_PICKER_MAC},
    {TabOrganizationController::kMoveToPreviousTask,
     IDS_UW_MOVE_TO_PREVIOUS_TASK_MAC},
    {TabOrganizationController::kMoveToNextTask, IDS_UW_MOVE_TO_NEXT_TASK_MAC},
    {TabOrganizationController::kScratchpad, IDS_UW_MOVE_TO_SCRATCHPAD_MAC},
    {TabOrganizationController::kKeepHere, IDS_UW_KEEP_HERE_MAC},
    {kGoToScratchpad, IDS_UW_KEY_CARD_GO_TO_SCRATCHPAD},
    {kTidyScratchpad, IDS_UW_TIDY_SCRATCHPAD_MAC},
    {kClearScratchpad, IDS_UW_CLEAR_SCRATCHPAD_MAC},
    {kRenameTask, IDS_UW_RENAME_TASK_MAC},
    {kNameTask, IDS_UW_NAME_TASK_MAC},
    {IDC_CLOSE_TAB_GROUP, IDS_UW_CLOSE_TASK_MAC},
    {kConfirmSuggestion, IDS_UW_CONFIRM_SUGGESTION_MAC},
    {TabOrganizationController::kUndo, IDS_UW_UNDO_ORGANIZATION_MAC},
    {kShowOrganizationSettings, IDS_UW_ORGANIZATION_SETTINGS_MAC},
    {TabOrganizationController::kNewChildTab, IDS_UW_NEW_CHILD_TAB_MAC},
    {TabOrganizationController::kNest, IDS_UW_NEST_BRANCH_MAC},
    {TabOrganizationController::kOutdent, IDS_UW_OUTDENT_BRANCH_MAC},
    {TabOrganizationController::kMoveUp, IDS_UW_MOVE_BRANCH_UP_MAC},
    {TabOrganizationController::kMoveDown, IDS_UW_MOVE_BRANCH_DOWN_MAC},
    {TabOrganizationController::kToggleBranch, IDS_UW_COLLAPSE_BRANCH_MAC},
    {TabOrganizationController::kCloseBranch, IDS_UW_CLOSE_BRANCH_MAC},
    {kSplitRight, IDS_UW_SPLIT_RIGHT_MAC},
    {kSplitDown, IDS_UW_SPLIT_DOWN_MAC},
    {kMaximizePane, IDS_UW_MAXIMIZE_PANE_MAC},
    {kMoveOutOfSplit, IDS_UW_MOVE_OUT_OF_SPLIT_MAC},
    {kSeparatePanes, IDS_UW_SEPARATE_PANES_MAC},
    {kNewTerminalCommand, IDS_UW_NEW_TERMINAL},
    {kNewChatCommand, IDS_UW_NEW_CHAT},
    {kNewLittleWindowCommand, IDS_UW_NEW_LITTLE_WINDOW_MAC},
    {kNewWorkspaceCommand, IDS_UW_NEW_WORKSPACE},
    {kShowThemePicker, IDS_UW_THEME_MENU},
    {kShowShortcutSettings, IDS_UW_KEYBOARD_SHORTCUTS_MAC},
    {kReportIssue, IDS_UW_REPORT_ISSUE_MAC},
};

struct ChromiumCommand {
  int id;
  const char16_t* name;
};

// Chromium names these differently in each menu and on each platform, so the
// command bar names them once.
constexpr ChromiumCommand kChromiumCommands[] = {
    {IDC_NEW_WINDOW, u"New Window"},
    {IDC_NEW_INCOGNITO_WINDOW, u"New Incognito Window"},
    {IDC_RESTORE_TAB, u"Reopen Closed Tab"},
    {IDC_DUPLICATE_TAB, u"Duplicate Tab"},
    {IDC_CLOSE_TAB, u"Close Tab"},
    {IDC_CLOSE_WINDOW, u"Close Window"},
    {IDC_RELOAD, u"Reload"},
    {IDC_RELOAD_BYPASSING_CACHE, u"Hard Reload"},
    {IDC_FIND, u"Find"},
    {IDC_PRINT, u"Print"},
    {IDC_SAVE_PAGE, u"Save Page As"},
    {IDC_BOOKMARK_THIS_TAB, u"Bookmark This Tab"},
    {IDC_SHOW_BOOKMARK_MANAGER, u"Bookmark Manager"},
    {IDC_SHOW_HISTORY, u"History"},
    {IDC_SHOW_DOWNLOADS, u"Downloads"},
    {IDC_OPTIONS, u"Settings"},
    {IDC_MANAGE_EXTENSIONS, u"Extensions"},
    {IDC_CLEAR_BROWSING_DATA, u"Delete Browsing Data"},
    {IDC_DEV_TOOLS, u"Developer Tools"},
    {IDC_VIEW_SOURCE, u"View Page Source"},
    {IDC_TASK_MANAGER_MAIN_MENU, u"Task Manager"},
    {IDC_ZOOM_PLUS, u"Zoom In"},
    {IDC_ZOOM_MINUS, u"Zoom Out"},
    {IDC_ZOOM_NORMAL, u"Actual Size"},
    {IDC_FULLSCREEN, u"Full Screen"},
};

CommandBar::Command Listed(BrowserWindowInterface* window,
                           int id,
                           std::u16string name) {
  CommandBar::Command command{id, std::move(name), {}};
  ui::Accelerator key;
  if (GetCommandAccelerator(window, id, &key)) {
    command.keys = KeyCaps(key);
  }
  return command;
}

}  // namespace

std::vector<CommandBar::Command> ListCommandBarCommands(
    BrowserWindowInterface* window) {
  std::vector<CommandBar::Command> commands;
  for (const UwCommand& command : kUwCommands) {
    if (CanRunCommand(window, command.id)) {
      commands.push_back(Listed(
          window, command.id,
          GetWindowCommandTitle(window, command.id)
              .value_or(l10n_util::GetStringUTF16(command.name))));
    }
  }
  for (const ChromiumCommand& command : kChromiumCommands) {
    if (CanRunCommand(window, command.id)) {
      commands.push_back(Listed(window, command.id, command.name));
    }
  }
  return commands;
}

}  // namespace uw
