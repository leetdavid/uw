# Keyboard and mouse

Status: scope agreed on 2026-10-14. T53, the menu bar entries and keys, and T57, branch collapse, are built and in review. The user asked for T57 on 2026-10-15. The other tickets wait for approval. The [command bar](command-bar.md) follows this plan and runs the commands it defines.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). The menu entries and keys are routine. Branch collapse is the risk: Chromium hides only whole tab groups, so uw has to hide single rows in the vertical tab strip and teach tab switching to skip them.

Designs go on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Keyboard and mouse". Branch collapse is drawn there.

## Agreed scope

- Every uw command has a menu bar entry with its key. Tab and branch commands go in Chromium's Tab menu. Task and organizer commands go in a new Tasks menu. Context menus show the same keys.
- Help → Search finds any of them by name. macOS System Settings → Keyboard → App Shortcuts can rebind any of them by title.
- Every key includes Cmd, so it still works while a [terminal tab](../CONTEXT.md#language) has focus. A page that handles a key itself keeps it, as Chromium does for its own keys.
- Tasks do not nest. A [tab branch](../CONTEXT.md#language) is the only level below a task. [ADR 0002](../adr/0002-tasks-are-native-tab-groups.md) stands.

### Keys

| Command | Key | Today |
| --- | --- | --- |
| Nest under previous tab | Ctrl+Cmd+→ | menu only |
| Move up one level | Ctrl+Cmd+← | menu only |
| Move branch up or down among its siblings | Ctrl+Cmd+↑ / ↓ | Chromium's Ctrl+Shift+PgUp/PgDn, which a terminal tab takes |
| Collapse or expand branch | Ctrl+Cmd+. | new |
| Close branch | Option+Cmd+W | menu only |
| New task | Ctrl+Cmd+P | exists |
| Move to new task | Ctrl+Cmd+N | menu only |
| Move to Scratchpad | Ctrl+Cmd+S | menu only |
| Go to task 1 to 9 | Ctrl+Cmd+1 to 9 | new. Selects the tab last used in that task. |
| Go to the Scratchpad | Ctrl+Cmd+0 | new |
| Rename task | Ctrl+Cmd+R | new. Opens the task's name field. |
| Close task, next task, previous task | Ctrl+Cmd+W, X, Z | Chromium's, unchanged |
| Confirm the organizer's suggestion | Ctrl+Cmd+Return | button on the activity card |
| Undo tab organization | Option+Cmd+Z | exists |
| Focus the sidebar | Ctrl+Cmd+L | new |
| Go to this task's terminal, and back | Ctrl+` | changes. Today it opens a new terminal. |
| New terminal | Ctrl+Shift+` | new |
| Move to task, pause, organizer activity, Suggest a name | menu, and the command bar later | no key |

### Terminal key

- Ctrl+` selects the terminal tab used last in the current task, or in the Scratchpad when the selected tab is ungrouped. With none there, it opens one where a blank new tab would open.
- Pressed in a terminal tab, Ctrl+` returns to the tab selected before it.
- Ctrl+Shift+` always opens a new terminal tab.

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
- Horizontal tabs show every tab, and the command is off there. A branch keeps its collapse for the sidebar, unless a tab in it is selected.
- Drags and the chevron count as [manual adjustments](../CONTEXT.md#language), like the menu commands.

### Mouse actions

Each is on by default and has a switch in a new "Mouse" section of uw's settings page.

| Action | Result |
| --- | --- |
| Option-click a tab's close button | Closes its branch. |
| Hover a task's folder row | Shows a "+" that opens a new tab in that task. |
| Double-click empty space below the tabs | Opens a new tab. |
| Middle-click a task's folder row | Closes the task and its tabs. |

Once the command bar lands, the "+" and the double-click open it instead of a blank tab.

### Sidebar keys

- Ctrl+Cmd+L moves keyboard focus to the tab list. Chromium's pane focus keys reach it too.
- ↑ and ↓ move between rows. → expands a collapsed branch or task, or steps into it. ← collapses it, or steps to the parent. Return selects the tab. Esc returns focus to the page.
- VoiceOver reads each tab's nesting level.

## Non-goals

- Tasks inside tasks.
- Keys that work outside uw.
- Recording your own keys. That is in the [command bar plan](command-bar.md).

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

## Stages

Each stage is a ticket on the [board](roadmap.md#board). Stages with a visible change need a design first.

1. T53: menu bar entries and Ctrl+Cmd keys.
2. T54: Ctrl+` toggles to the task's terminal.
3. T55: dragging a parent carries its branch.
4. T56: sideways drag sets the depth. Design first.
5. T57: branch collapse, with its key. Design first.
6. T58: mouse actions and their settings. Design first.
7. T59: the sidebar as a keyboard tree.

## Acceptance checks

1. Each key in the [table](#keys) runs its command in a headless window through a real key event. With a terminal tab selected it is checked by hand: a headless window sends a key the focused page left alone back into the page, not to the browser.
2. The Tab and Tasks menus list every command with its key. Help → Search finds "Move to Scratchpad".
3. In a task with a terminal tab, Ctrl+` selects it and a second press returns to the page. In a task without one, Ctrl+` opens one below the active tab's branch. Ctrl+Shift+` opens a second one.
4. Dragging a parent moves its branch with its nesting intact. Option-drag moves the parent alone and its children move up one level.
5. A tab dropped with the pointer to the right becomes a child of the tab above. Dropped with the pointer at the left, it sits at the task's top level.
6. A collapsed branch hides its children and shows their count. Ctrl+Tab skips them. Cmd+1 to 9 onto a hidden tab expands the branch. Closing the collapsed parent shows its children one level up. The collapse survives a restart, and an organizer move leaves it collapsed.
7. Each mouse action works, and does nothing once its switch is off.
8. Ctrl+Cmd+L focuses the tab list. The arrow keys walk the tree as described, and Esc returns focus to the page. The accessibility tree gives each tab row its level.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Whether window manager apps on the user's Mac already take Ctrl+Cmd with the arrows. Check by hand before T53 ships.
- What sideways drags do in horizontal tabs. Proposal: they are off there. Collapse was settled with T57: see [Branches with the mouse](#branches-with-the-mouse).
- Where Ctrl+` returns to when the tab selected before the terminal has closed. Proposal: the tab above the terminal.
