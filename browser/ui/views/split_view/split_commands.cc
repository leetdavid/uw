// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/split_view/split_commands.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/components/split_view/split_layout.h"

namespace uw {
namespace {

SplitDirection DirectionOf(int command) {
  return command == kSplitDown ? SplitDirection::kStacked
                               : SplitDirection::kSideBySide;
}

MultiContentsView* PageArea(const tabs::TabInterface* tab) {
  BrowserView* view =
      BrowserView::GetBrowserViewForBrowser(tab->GetBrowserWindowInterface());
  return view ? view->multi_contents_view() : nullptr;
}

// Whether cutting the pane of `tab` in two the `direction` way leaves both
// halves at least kMinPaneSize each way.
bool HasRoomToSplit(tabs::TabInterface* tab, SplitDirection direction) {
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  MultiContentsView* page_area = PageArea(tab);
  if (!page_area) {
    return false;
  }
  SplitLayout panes(direction);
  size_t pane = 0;
  size_t added = 1;
  if (const std::optional<split_tabs::SplitTabId> split = tab->GetSplit()) {
    const split_tabs::SplitTabData* data =
        window->GetTabStripModel()->GetSplitData(*split);
    const std::vector<tabs::TabInterface*> tabs = data->ListTabs();
    panes = data->visual_data()->panes();
    pane = std::ranges::find(tabs, tab) - tabs.begin();
    added = panes.Split(pane, direction);
  }
  const std::vector<gfx::Rect> bounds = page_area->GetPaneBounds(panes);
  return std::ranges::all_of(std::vector<size_t>{pane, added}, [&](size_t i) {
    return bounds[i].width() >= kMinPaneSize &&
           bounds[i].height() >= kMinPaneSize;
  });
}

}  // namespace

bool IsSplitCommand(int command) {
  return command >= kSplitRight && command <= kMaximizePane;
}

bool CanRunSplitCommand(tabs::TabInterface* tab, int command) {
  if (!tab || tab->IsPinned() ||
      tab->GetBrowserWindowInterface()->GetType() !=
          BrowserWindowInterface::TYPE_NORMAL) {
    return false;
  }
  switch (command) {
    case kSplitRight:
    case kSplitDown:
      return HasRoomToSplit(tab, DirectionOf(command));
    case kMoveOutOfSplit:
    case kSeparatePanes:
      return tab->IsSplit();
    case kMaximizePane:
      return tab->IsSplit() && PageArea(tab);
  }
  return false;
}

void RunSplitCommand(tabs::TabInterface* tab, int command) {
  if (!CanRunSplitCommand(tab, command)) {
    return;
  }
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  TabStripModel* model = window->GetTabStripModel();
  if (command == kMoveOutOfSplit) {
    model->MoveTabOutOfSplit(model->GetIndexOfTab(tab));
    return;
  }
  if (command == kSeparatePanes) {
    model->RemoveSplit(*tab->GetSplit());
    return;
  }

  // The new tab opens as Cmd+T's does, after the tab it splits.
  model->ActivateTabAt(model->GetIndexOfTab(tab));
  content::WebContents* contents = tab->GetContents();
  if (command == kMaximizePane || IsMaximized(tab)) {
    PageArea(tab)->ToggleMaximized(contents);
  }
  if (command == kMaximizePane) {
    return;
  }
  const bool terminal = IsTerminalUrl(contents->GetLastCommittedURL());
  if (terminal) {
    OpenTerminalTabIn(window, TerminalFolder(contents));
  } else {
    chrome::NewTab(window, NewTabTypes::kNewTabCommand);
  }
  tabs::TabInterface* pane = model->GetActiveTab();
  if (!pane || pane == tab || pane->IsSplit()) {
    return;
  }
  model->SplitPane(model->GetIndexOfTab(tab), model->GetIndexOfTab(pane),
                   command == kSplitDown
                       ? split_tabs::SplitTabLayout::kStacked
                       : split_tabs::SplitTabLayout::kSideBySide);
  if (!terminal) {
    CommandBar::OpenForPane(window, pane);
  }
}

bool IsMaximized(const tabs::TabInterface* tab) {
  const MultiContentsView* page_area = tab ? PageArea(tab) : nullptr;
  return page_area && tab->IsSplit() &&
         page_area->GetMaximizedContents() == tab->GetContents();
}

int MaximizePaneTitle(const tabs::TabInterface* tab) {
  return IsMaximized(tab) ? IDS_UW_RESTORE_PANES_MAC : IDS_UW_MAXIMIZE_PANE_MAC;
}

void AddSplitCommands(ui::SimpleMenuModel* menu,
                      const tabs::TabInterface* tab) {
  if (!tab || tab->IsPinned()) {
    return;
  }
  std::optional<size_t> at;
  for (int command : {TabStripModel::CommandAddToSplit,
                      TabStripModel::CommandSwapWithActiveSplit,
                      TabStripModel::CommandArrangeSplit}) {
    at = menu->GetIndexOfCommandId(command);
    if (at) {
      break;
    }
  }
  if (!at) {
    return;
  }
  menu->RemoveItemAt(*at);
  const auto insert = [&](int command, int string) {
    menu->InsertItemAt((*at)++, command, l10n_util::GetStringUTF16(string));
  };
  insert(kSplitRight, IDS_UW_SPLIT_RIGHT_MAC);
  insert(kSplitDown, IDS_UW_SPLIT_DOWN_MAC);
  if (tab->IsSplit()) {
    insert(kMaximizePane, MaximizePaneTitle(tab));
    insert(kMoveOutOfSplit, IDS_UW_MOVE_OUT_OF_SPLIT_MAC);
    insert(kSeparatePanes, IDS_UW_SEPARATE_PANES_MAC);
  }
}

}  // namespace uw
