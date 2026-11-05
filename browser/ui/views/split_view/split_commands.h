// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_SPLIT_VIEW_SPLIT_COMMANDS_H_
#define UW_BROWSER_UI_VIEWS_SPLIT_VIEW_SPLIT_COMMANDS_H_

namespace tabs {
class TabInterface;
}

namespace ui {
class SimpleMenuModel;
}

namespace uw {

// A split that would make a pane narrower or shorter than this does nothing.
inline constexpr int kMinPaneSize = 200;

// Split Right, Split Down, Maximize Pane, Move Out of Split View, and Separate
// Panes, each acting on one tab. A pin has none of them yet.
bool IsSplitCommand(int command);
bool CanRunSplitCommand(tabs::TabInterface* tab, int command);
// Split Right and Split Down cut the pane of `tab` in two and select a new
// tab in the new half: a terminal in the same folder from a terminal tab, and
// what Cmd+T gives from any other. A maximized pane is restored first.
// Maximize Pane selects the tab and maximizes its pane, or restores it.
void RunSplitCommand(tabs::TabInterface* tab, int command);

// Whether the pane of `tab` is maximized in its split.
bool IsMaximized(const tabs::TabInterface* tab);
// Maximize Pane's title for `tab`: Restore Panes while it is maximized.
int MaximizePaneTitle(const tabs::TabInterface* tab);

// Puts the commands for `tab` in its menu, in place of Chromium's split
// items.
void AddSplitCommands(ui::SimpleMenuModel* menu, const tabs::TabInterface* tab);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_SPLIT_VIEW_SPLIT_COMMANDS_H_
