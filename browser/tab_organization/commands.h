// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_
#define UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_

namespace uw {

// Commands of the menu bar and keys that act on the window's selected tab,
// its task, its workspace, or automatic organization. The numbers follow
// TabOrganizationController::Command, whose commands also run as window
// commands, on the selected tabs.
enum WindowCommand {
  kGoToTask1 = 56020,
  kGoToTask9 = kGoToTask1 + 8,
  kGoToScratchpad,
  kRenameTask,
  kNameTask,
  kConfirmSuggestion,
  kPauseOrganizationForAnHour,
  kPauseOrganizationUntilLaunch,
  kPauseOrganizationIndefinitely,
  kResumeOrganization,
  kShowOrganizerActivity,
  kShowOrganizationSettings,
  kShowThemePicker,
  kGoToWorkspace1,
  kGoToWorkspace9 = kGoToWorkspace1 + 8,
  kNextWorkspace,
  kPreviousWorkspace,
  kShowShortcutSettings,
  // Opens the watch card under the eye in the address bar.
  kWatchThisPage,
  kClearScratchpad,
  kTidyScratchpad,
  kReportIssue,
  // Opens uw://scheduled, every watched page and scheduled chat.
  kShowScheduled,
  // Opens the Inbox, as its button in the sidebar does.
  kShowInbox,
  kLastWindowCommand = kShowInbox,
};

bool IsWindowCommand(int command);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_
