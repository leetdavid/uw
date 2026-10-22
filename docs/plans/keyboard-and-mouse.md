# Keyboard and mouse

Status: scope agreed on 2026-10-14. T53, the menu bar entries and keys, and T57, branch collapse, are built and in review. The user asked for T57 on 2026-10-15. [New child tab](#new-child-tab), T73 and T74, was agreed on 2026-10-15 and comes next, then the [command bar](command-bar.md). The [tab menu](#tab-menu) order, the [new button](#new-button), and the new task keys were agreed on 2026-10-15 as T87, T88, and T90. The [mouse actions](#mouse-actions), T58, were agreed on 2026-10-16, with T55, dragging a branch, after them. The [Keyboard shortcuts](#keyboard-shortcuts) section, T122, and the Keep here key were agreed on 2026-10-19. The other tickets wait for approval. The command bar runs the commands this plan defines.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). The menu entries and keys are routine. Branch collapse is the risk: Chromium hides only whole tab groups, so uw has to hide single rows in the vertical tab strip and teach tab switching to skip them.

Designs go on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Keyboard and mouse". Branch collapse is drawn there.

## Agreed scope

- Every uw command has a menu bar entry with its key. Tab and branch commands go in Chromium's Tab menu. Task and organizer commands go in a new Tasks menu. Context menus show the same keys.
- Help → Search finds any of them by name. The [Keyboard shortcuts](#keyboard-shortcuts) section lists every key and changes uw's.
- Every key includes Cmd, so it still works while a [terminal tab](../CONTEXT.md#language) has focus. A page that handles a key itself keeps it, as Chromium does for its own keys.
- Tasks do not nest. A [tab branch](../CONTEXT.md#language) is the only level below a task. [ADR 0002](../adr/0002-tasks-are-native-tab-groups.md) stands.

### Keys

| Command | Key | Today |
| --- | --- | --- |
| Nest under previous tab | Ctrl+Cmd+→ | menu only |
| Move up one level | Ctrl+Cmd+← | menu only |
| New child tab | Ctrl+Cmd+T | new |
| Move branch up or down among its siblings | Ctrl+Cmd+↑ / ↓ | Chromium's Ctrl+Shift+PgUp/PgDn, which a terminal tab takes |
| Collapse or expand branch | Ctrl+Cmd+. | new |
| Close branch | Option+Cmd+W | menu only |
| New task | Ctrl+Cmd+N | changes. Was Chromium's Ctrl+Cmd+P, which is removed. |
| Move to new task | Shift+Ctrl+Cmd+N | changes. Was Ctrl+Cmd+N. |
| Move to Scratchpad | Ctrl+Cmd+S | menu only |
| Keep here | Ctrl+Cmd+K | new. Makes a [kept tab](../CONTEXT.md#language), or lets it go. See [Automatic organization](automatic-organization.md#agreed-scope). |
| Go to task 1 to 9 | Ctrl+Cmd+1 to 9 | new. Selects the tab last used in that task. |
| Go to the Scratchpad | Ctrl+Cmd+0 | new |
| Rename task | Ctrl+Cmd+R | new. Turns the name on the task's row into a field. |
| Close task, next task, previous task | Ctrl+Cmd+W, X, Z | Chromium's, unchanged |
| Confirm the organizer's suggestion | Ctrl+Cmd+Return | button on the activity card |
| Undo tab organization | Option+Cmd+Z | exists |
| Focus the sidebar | Ctrl+Cmd+L | new |
| Go to this task's terminal, and back | Ctrl+\` | changes. Today it opens a new terminal. |
| New terminal | Ctrl+Shift+\` | new |
| Run a shell command from the command bar | Cmd+Shift+P | new. Opens the [command bar](command-bar.md#sending-the-text-to-a-chat-or-a-terminal) with `>`. |
| Move to task, pause, organizer activity, Name Task | menu, and the command bar later | no key |

### Terminal key

- Ctrl+\` selects the terminal tab used last in the current task, or in the Scratchpad when the selected tab is ungrouped. With none there, it opens one where a blank new tab would open.
- Pressed in a terminal tab, Ctrl+\` returns to the tab selected before it.
- Ctrl+Shift+\` always opens a new terminal tab.

### Keyboard shortcuts

Agreed on 2026-10-19 as T122. uw keeps its own list of keys, so the same code can serve a Windows build. See [ADR 0011](../adr/0011-uw-keeps-its-own-key-list.md).

- A Keyboard shortcuts section in uw's settings, at `uw://settings/shortcuts`, lists every command in the menu bar with its key, grouped by menu: uw's and Chromium's.
- A click on a uw command's key lets the user press a new one, in the key recorder Chromium uses on `chrome://extensions/shortcuts`. Chromium's own keys show but cannot be changed yet.
- A key must include Cmd, so it works while a terminal tab has focus. On Windows that would be Ctrl.
- A key already in use moves to the new command, whether a uw or a Chromium command had it. The row that lost it reads "No key" and shows the old key.
- Each row has Reset, back to uw's default, and can have no key. The section has Reset all.
- The keys are the same in every profile and workspace.
- Settings search finds a command by name. Help → Keyboard Shortcuts… opens the section, and so will the command bar.
- A key recorded from the [command bar](command-bar.md#aliases-and-recorded-keys) lands in the same list.
- The design is drawn on the canvas page "Keyboard and mouse", in the frames "Keyboard shortcuts: settings" and "Keyboard shortcuts: every state". It waits for the user's sign-off.

### New child tab

Agreed on 2026-10-15, at the user's request.

- "New child tab" in a tab's right-click menu sits under "New tab below". It opens a blank tab, selects it, and places it first under that tab: one level deeper, above the tab's other children. A link opened from the tab later still goes after its last child.
- The Tab menu entry and Ctrl+Cmd+T do the same for the selected tab.
- "New tab below" opens below the right-clicked tab's branch, at its level. Today it opens below the selected tab's branch.
- With several tabs selected, it opens one child, under the tab that was right-clicked.
- The child is in its parent's task, or in the Scratchpad. Selecting it expands a collapsed parent. Organization undo does not remove it.
- It is off for pinned tabs.
- Terminal and chat tabs can have children like any tab.
- A child made this way, or with "Nest under previous tab", stays with its parent. See [Branch moves](automatic-organization.md#branch-moves).
- Once the command bar lands, the command opens it instead of a blank tab, per the [command bar plan](command-bar.md#opening-it).

### Tab menu

Agreed on 2026-10-15. A tab's right-click menu groups moves together, tree commands together, and every close at the end:

1. New tab below, New child tab, and Chromium's split view items.
2. Move to task ▸ (its list ends with New task), Move to new task, Move to Scratchpad, Move to another window ▸, and Keep here.
3. Nest under previous tab, Move up one level, Collapse branch, and Undo tab organization.
4. Reload, Duplicate, Pin, and Mute site.
5. Chromium's Read later and Send to your devices.
6. Close tab, Close branch, Close other tabs, and Close tabs below.

Chromium's "Add tab to new group" and "Remove from group" are left out: see [Product](../product.md#automatic-tab-organization).

### New button

Agreed on 2026-10-15.

- A split button, "+" and "▾", sits at the right end of the sidebar's foot, with the Organize button at the left. The "+" under the tab list goes away.
- A click on "+" does what Cmd+T does: a new tab below the current one, in its task, or the command bar when that is on.
- A click on "▾" opens a menu above the button, over the page, with each item's key: New tab, New task, New chat, New terminal, and New split view.
- A collapsed sidebar hides Organize and "▾" and keeps a lone "+" at its foot. A right-click on it opens the same menu.
- The design is the frames "New button: the sidebar foot" and "New button: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-15. The user signed off the same day once the button moved from beside Organize to the right end.

### Branches with the mouse

- Dragging a parent tab carries its whole branch and keeps its nesting. Option-drag moves only that tab, and its children move up one level as they do today.
- While a tab is dragged, its indent follows the pointer's horizontal position. It can sit anywhere from the task's top level to one level deeper than the tab above it. The drop keeps the depth shown.
- While the pointer is on a parent tab, a chevron takes the place of its favicon. A click collapses or expands its branch and leaves the selection alone. A tab at the top level has no indent space, so the chevron cannot sit there.
- A collapsed parent keeps its favicon and ends its row with the number of tabs it hides, in the muted text color. The title gives way to it.
- Collapsed branches follow Chromium's rules for collapsed tab groups:
  - Ctrl+Tab and next or previous tab skip hidden tabs.
  - Selecting a hidden tab any other way expands its branch. This covers Cmd+1 to 9, the command bar, a link opened in front, and the organizer moving the selected tab.
  - Collapsing the branch of the selected tab selects the parent at its top.
  - Closing a collapsed parent moves its children up one level, where they show again.
  - A link opened in the background from a collapsed branch stays hidden, and the count goes up.
  - The collapse survives a restart. The organizer and undo never change it.
- Selecting a hidden tab expands its branch.
- Drags and the chevron count as [manual adjustments](../CONTEXT.md#language), like the menu commands.

### Mouse actions

Agreed on 2026-10-16 as T58. Each is on by default and has a switch in a new "Mouse" section of uw's settings page. A switch turned off leaves that click as Chromium has it.

| Action | Result |
| --- | --- |
| Option-click a tab's close button | Closes its branch, like Close branch. |
| Hover a task's folder row | Shows a "+" before the task's menu button. A click opens the [command bar](command-bar.md#opening-it) for the task. Tooltip "New tab in task". With the command bar switched off, a click opens a blank tab at the end of the task and selects it. |
| Double-click empty space below the tabs | Does what the foot's "+" does. The space still drags the window, but a double-click there no longer zooms it, as in Arc. |
| Middle-click a task's folder row | Closes the task and its tabs, like Close task. |

- The "+" and the double-click are in the wide sidebar only. Option-click and middle-click also work in the narrow sidebar.
- The task row's "+" opens the command bar since it landed. Agreed on 2026-10-21.
- Elsewhere in the window, a double-click on space that drags the window still does what macOS's "Double-click a window's title bar" setting says. A double-click below the tabs did too until T58.
- The design is the frames "Mouse actions: the sidebar", "Mouse actions: settings", and "Mouse actions: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-16. The user signed off on 2026-10-19.

### Menu of empty sidebar space

Right-clicking empty space in the sidebar shows a menu with `Theme…`, which opens the [theme picker](themes.md). Built in T96.

### Sidebar keys

- Ctrl+Cmd+L moves keyboard focus to the tab list. Chromium's pane focus keys reach it too.
- ↑ and ↓ move between rows. → expands a collapsed branch or task, or steps into it. ← collapses it, or steps to the parent. Return selects the tab. Esc returns focus to the page.
- VoiceOver reads each tab's nesting level.

## Non-goals

- Tasks inside tasks.
- Keys that work outside uw.
- Changing Chromium's own keys, such as Find Next. That is T123, later.

## Design

- uw's commands are defined once: a number from the `TabOrganizationController::Command` range, a string, and a key. The menu bar, the tab context menu, and later the command bar read that list. No `IDC_` value is added, because `chrome_command_ids.h` rebuilds thousands of files.
- The Tasks menu is built in `main_menu_builder.mm` beside the Tab menu, so each item carries its key equivalent.
- uw's menu keys run like Chromium's shortcuts that are not in the menu: the key lookup hands them to the command code, not to `NSMenu`, which serves only the window in front. A page still sees each key first. Headless tests can press them.
- The menu bar, the keys, the tab menu, and the Organize menu all run commands through `browser/ui/views/tab_organization/window_commands`.
- Branch drags extend Chromium's drag set to the branch in `TabDragHandlerImpl::InitializeDrag`, which uw already hooks. Chromium already drags several selected tabs together.
- The page tree's `Adopt` takes the depth the drop asked for. `tab_indent` shows it while the drag runs.
- `TabOrganizationController` keeps which parents are collapsed and saves it with the session data that already carries parent links. Collapsing is not an organization action, so it is not in the undo history.
- Both vertical layouts that `tab-tree.patch` changes give a hidden row no height. The tab row places the chevron and the count from `browser/ui/views/tab_organization/branch_toggle`.
- `TabStripModel::IsTabCollapsed` also asks `HiddenTabs`, which the controller provides through the window. Ctrl+Tab and the choice of the next tab after a close already skip collapsed groups through it.
- The mouse switches are profile prefs read by the views they change.
- The empty space below the tabs drags the window, so macOS, not the views, gets its clicks. While macOS hands out the second click of a double-click, the space counts as the views' instead, and Chromium's double-click-to-zoom never sees it.
- "New child tab" is a `TabOrganizationController::Command`. The page tree places the new tab as the first child.
- uw's key list lives in local state, beside the glass frame switch. The key lookup, the menus, and the command bar read it.
- `TabOrganizationController` keeps which children were nested by hand and saves it with the parent links. `PlaceBranch` takes them along with their parent.

## Stages

Each stage is a ticket on the [board](roadmap.md#board). Stages with a visible change need a design first.

1. T53: menu bar entries and Ctrl+Cmd keys.
2. T54: Ctrl+\` toggles to the task's terminal.
3. T55: dragging a parent carries its branch.
4. T56: sideways drag sets the depth. Design first.
5. T57: branch collapse, with its key. Design first.
6. T58: mouse actions and their settings. Design first.
7. T59: the sidebar as a keyboard tree.
8. T73: New child tab, its key, and "New tab below" by the right-clicked tab.
9. T74: children nested by hand stay with their parent.
10. T87: the tab menu in the agreed order.
11. T88: the new button. Design first.
12. T90: Ctrl+Cmd+N for New task and Shift+Ctrl+Cmd+N for Move to new task.
13. T122: the Keyboard shortcuts section. Design first.

T73, T74, and the command bar are built next, then the other stages. Changed on 2026-10-15. T54 is built in the command bar's order, before T82, which needs it.

## Acceptance checks

1. Each key in the [table](#keys) runs its command in a headless window through a real key event. With a terminal tab selected it is checked by hand: a headless window sends a key the focused page left alone back into the page, not to the browser.
2. The Tab and Tasks menus list every command with its key. Help → Search finds "Move to Scratchpad".
3. In a task with a terminal tab, Ctrl+\` selects it and a second press returns to the page. In a task without one, Ctrl+\` opens one below the active tab's branch. Ctrl+Shift+\` opens a second one.
4. Dragging a parent moves its branch with its nesting intact. Option-drag moves the parent alone and its children move up one level.
5. A tab dropped with the pointer to the right becomes a child of the tab above. Dropped with the pointer at the left, it sits at the task's top level.
6. A collapsed branch hides its children and shows their count. Ctrl+Tab skips them. Cmd+1 to 9 onto a hidden tab expands the branch. Closing the collapsed parent shows its children one level up. The collapse survives a restart, and an organizer move leaves it collapsed.
7. Each mouse action works, and does nothing once its switch is off.
8. Ctrl+Cmd+L focuses the tab list. The arrow keys walk the tree as described, and Esc returns focus to the page. The accessibility tree gives each tab row its level.
9. "New child tab" on a tab that is not selected and has children opens a selected blank tab directly under it, above those children. Ctrl+Cmd+T does the same for the selected tab. "New tab below" on a tab that is not selected opens below that tab's branch.
10. A child nested by hand moves with its parent when the organizer moves the branch, and is not filed alone when its page changes. A child opened from a link still can be. The hold survives a restart and ends when the parent closes.
11. A tab's right-click menu lists its items in the order of [Tab menu](#tab-menu), with no "Add tab to new group" or "Remove from group".
12. A click on the foot's "+" does what Cmd+T does, "▾" lists its five items with their keys, and the tab list has no "+". A collapsed sidebar shows a lone "+" whose right-click opens the same menu.
13. Ctrl+Cmd+N opens a new task, Shift+Ctrl+Cmd+N moves the selected branch to a new task, and Ctrl+Cmd+P does nothing.
14. The Keyboard shortcuts section lists uw's and Chromium's keys by menu. A new key for a uw command runs it in a headless window, after a restart too, and in every profile. A key without Cmd is refused. Taking Cmd+G moves it from Find Next, whose row then reads "No key". Reset and Reset all bring back the defaults.
15. Ctrl+Cmd+K keeps the selected tab and a second press lets it go. Keep here is in the tab's right-click menu and the Tasks menu.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Whether window manager apps on the user's Mac already take Ctrl+Cmd with the arrows. Check by hand before T53 ships.
- Where Ctrl+\` returns to when the tab selected before the terminal has closed. Proposal: the tab above the terminal.
