// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/window_commands.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr char kSettingsSubPage[] = "uwOrganization";

std::optional<tab_groups::TabGroupId> SelectedTask(
    BrowserWindowInterface* window) {
  tabs::TabInterface* tab = window->GetTabStripModel()->GetActiveTab();
  return tab ? tab->GetGroup() : std::nullopt;
}

TabGroupHeaderView* HeaderFor(views::View* view,
                              const tab_groups::TabGroupId& group) {
  if (auto* header = views::AsViewClass<TabGroupHeaderView>(view);
      header && header->group() == group) {
    return header;
  }
  for (views::View* child : view->children()) {
    if (TabGroupHeaderView* header = HeaderFor(child, group)) {
      return header;
    }
  }
  return nullptr;
}

}  // namespace

bool CanRunWindowCommand(BrowserWindowInterface* window, int command) {
  TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (!controller || !IsWindowCommand(command)) {
    return false;
  }
  if (TabOrganizationController::IsCommand(command)) {
    return controller->CanExecute(command, controller->TargetsFor(command));
  }
  if (command >= kGoToTask1 && command <= kGoToTask9) {
    return static_cast<size_t>(command - kGoToTask1) <
           controller->tree().Tasks().size();
  }
  const AutomaticOrganizer* organizer = AutomaticOrganizer::From(window);
  const PrefService* prefs = window->GetProfile()->GetPrefs();
  const PauseMode pause = ReadPauseState(prefs);
  // Pausing means nothing while automatic organization is off.
  const bool on = prefs->GetBoolean(kOrganizationEnabledPref);
  switch (command) {
    case kGoToScratchpad:
      return std::ranges::any_of(
          controller->tree().pages(),
          [](const Page& page) { return page.task.empty(); });
    case kRenameTask:
      return SelectedTask(window).has_value();
    case kSuggestTaskName:
      return organizer && organizer->CanSuggestName() &&
             SelectedTask(window).has_value();
    case kConfirmSuggestion:
      return organizer && organizer->HasSuggestion();
    case kPauseOrganizationForAnHour:
      return on && pause != PauseMode::kTimed;
    case kPauseOrganizationUntilLaunch:
      return on && pause != PauseMode::kUntilLaunch;
    case kPauseOrganizationIndefinitely:
      return on && pause != PauseMode::kIndefinite;
    case kResumeOrganization:
      return on && pause != PauseMode::kOff;
    default:
      return true;
  }
}

void RunWindowCommand(BrowserWindowInterface* window, int command) {
  if (!CanRunWindowCommand(window, command)) {
    return;
  }
  TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (TabOrganizationController::IsCommand(command)) {
    controller->Execute(command, controller->TargetsFor(command));
    return;
  }
  if (command >= kGoToTask1 && command <= kGoToTask9) {
    controller->SelectTask(controller->tree().Tasks()[command - kGoToTask1]);
    return;
  }
  PrefService* prefs = window->GetProfile()->GetPrefs();
  switch (command) {
    case kGoToScratchpad:
      controller->SelectTask(std::string());
      break;
    case kRenameTask:
      if (TabGroupHeaderView* header =
              HeaderFor(BrowserView::GetBrowserViewForBrowser(window),
                        *SelectedTask(window))) {
        header->ShowContextMenuForViewImpl(
            header, gfx::Point(), ui::mojom::MenuSourceType::kKeyboard);
      }
      break;
    case kSuggestTaskName:
      AutomaticOrganizer::From(window)->SuggestName(
          SelectedTask(window)->ToString(), base::DoNothing());
      break;
    case kConfirmSuggestion:
      AutomaticOrganizer::From(window)->AcceptSuggestion();
      break;
    case kPauseOrganizationForAnHour:
      SetPauseState(prefs, PauseMode::kTimed, base::Hours(1));
      break;
    case kPauseOrganizationUntilLaunch:
      SetPauseState(prefs, PauseMode::kUntilLaunch);
      break;
    case kPauseOrganizationIndefinitely:
      SetPauseState(prefs, PauseMode::kIndefinite);
      break;
    case kResumeOrganization:
      SetPauseState(prefs, PauseMode::kOff);
      break;
    case kShowOrganizerActivity:
      prefs->SetBoolean(kOrganizationShowActivityPref,
                        !prefs->GetBoolean(kOrganizationShowActivityPref));
      break;
    case kShowOrganizationSettings:
      chrome::ShowSettingsSubPage(window, kSettingsSubPage);
      break;
  }
}

bool GetCommandAccelerator(BrowserWindowInterface* window,
                           int command,
                           ui::Accelerator* accelerator) {
  return BrowserView::GetBrowserViewForBrowser(window)->GetAccelerator(
      command, accelerator);
}

bool IsWindowCommandChecked(BrowserWindowInterface* window, int command) {
  return command == kShowOrganizerActivity &&
         window->GetProfile()->GetPrefs()->GetBoolean(
             kOrganizationShowActivityPref);
}

std::optional<std::u16string> GetWindowCommandTitle(
    BrowserWindowInterface* window,
    int command) {
  const TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (!controller || command != TabOrganizationController::kToggleBranch) {
    return std::nullopt;
  }
  return l10n_util::GetStringUTF16(
      controller->CollapsesBranches(controller->SelectionFor())
          ? IDS_UW_COLLAPSE_BRANCH_MAC
          : IDS_UW_EXPAND_BRANCH_MAC);
}

}  // namespace uw
