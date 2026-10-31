# Keyboard and mouse

Status: scope agreed on 2026-10-14. T53, the menu bar entries and keys, and T57, branch collapse, are built and in review. The user asked for T57 on 2026-10-15. [New child tab](#new-child-tab), T73 and T74, was agreed on 2026-10-15 and comes next, then the [command bar](command-bar.md). The [tab menu](#tab-menu) order, the [new button](#new-button), and the new task keys were agreed on 2026-10-15 as T87, T88, and T90. The [mouse actions](#mouse-actions), T58, were agreed on 2026-10-16, with T55, dragging a branch, after them. The [Keyboard shortcuts](#keyboard-shortcuts) section, T122, and the Keep here key were agreed on 2026-10-19. The [close button on a task's row](#close-button-on-a-tasks-row), T284, was agreed on 2026-10-31. [Sideways drag](#sideways-drag), T56, was agreed and its design signed off on 2026-10-28. It is built after T55. The other tickets wait for approval. The command bar runs the commands this plan defines.

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
| Hide or show the sidebar | Cmd+S | changes. Was Chromium's Cmd+Shift+L. Save Page As loses Cmd+S. See [Hidden sidebar](hidden-sidebar.md). |
| Go to this task's terminal, and back | Ctrl+\` | changes. Today it opens a new terminal. |
| New terminal | Ctrl+Shift+\` | new |
| Run a shell command from the command bar | Cmd+Shift+P | new. Opens the [command bar](command-bar.md#sending-the-text-to-a-chat-or-a-terminal) with `>`. |
| Move to task, pause, organizer activity, Name Task, [Tidy and Clear Scratchpad](scratchpad-buttons.md) | menu, and the command bar later | no key |

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
- Each row has Reset, back to uw's default, and can have no key. Reset gives the default key back even when another row has it then, and that row loses it. The section has Reset all, which asks nothing first.
- The menus are listed in menu bar order, from the uw menu to Help. A submenu's commands are rows of their own. The Services menu and the pages the History, Bookmarks, and Window menus list are left out.
- Keys read as the recorder writes them, such as "Command + Ctrl + K". It cannot record Return, Tab, \`, or brackets, so only Reset brings back a default that uses one, such as Ctrl+Cmd+Return.
- The keys are the same in every profile and workspace.
- Settings search finds a command by name. Help → Keyboard Shortcuts… opens the section, and so will the command bar.
- A key recorded from the [command bar](command-bar.md#aliases-and-recorded-keys) lands in the same list.
- The design is on the canvas page "Keyboard and mouse", in the frames "Keyboard shortcuts: settings" and "Keyboard shortcuts: every state", with its rules in the canvas guide `uw-keyboard-shortcuts`. The user signed it off on 2026-10-22.

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
- The design is the frames "New button: the sidebar foot" and "New button: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-15. The user signed off the same day once the button moved from beside Organize to the right end.

### Branches with the mouse

- Dragging a parent tab carries its whole branch and keeps its nesting. Option-drag moves only that tab, and its children move up one level as they do today.
- While a tab is dragged, up and down picks its place and sideways picks its depth. See [Sideways drag](#sideways-drag).
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

#### Sideways drag

Agreed on 2026-10-28 as T56. Before, a dropped tab joined the branch of the tab below it, so a child dragged to the bottom of its task could not leave its parent's branch.

- Up and down picks the tab's place, as in Chromium. A straight drag keeps the tab's parent anywhere inside that parent's branch, including just after its last child. Elsewhere the tab takes the level of the tab below it, or the task's top level when nothing is below. It never becomes the child of a tab it passes.
- Moving the pointer sideways changes the depth by one level per 16px, after a small dead zone, so a wobbly vertical drag keeps the depth. The dragged rows slide to each new indent, as Chromium moves its tabs, and the drop keeps the depth shown. Nothing else is drawn. The slide was asked for on 2026-10-28.
- The depth can go from the level of the tab below up to one deeper than the tab above, and no deeper than 6 levels. The tab below always keeps its parent. To leave a branch, drag past its last child and move left.
- The slot below a collapsed parent goes no deeper than the parent. To nest under it, expand it first.
- A dragged parent's branch keeps its shape. Up, down, or sideways, every tab in it moves by the same number of levels. The limits above apply to the parent, and the branch's deepest tab goes no deeper than 6 levels. Asked for on 2026-10-28.
- Several selected tabs land as siblings at the depth shown, each with its branch. Sideways counts from the depth of the row grabbed.
- Sideways never changes the task. The task is the one the tab is dropped in, as in Chromium. Dragging left past the sidebar's edge does not tear the tab off into a new window.
- A drop that gives a tab a new parent nests it by hand, like Nest under previous tab. A reorder inside the same branch does not. Dragging it out of the branch ends that.
- Esc during a drag puts the tabs back where they were, depths included, and nests nothing by hand.
- "Drag sideways to nest tabs", in the Mouse section of uw's settings and on by default, turns sideways off. A straight drag still follows the first rule.
- The design is the frames "Sideways drag: the sidebar", "Sideways drag: every state", and "Sideways drag: a branch" on the canvas page "Keyboard and mouse". The user signed them off on 2026-10-28.

### Side by side in the sidebar

Agreed on 2026-10-24 from the frames "Side by side: the sidebar" and "Side by side: every state". Chromium put the two tabs of a side-by-side view on one row, about 115px each, so their titles shrank to a few letters.

- The pair stacks: one full row per page, the left page on top, inside one box in the sidebar's tile fill. The box has 12px corners and 2px inside and between its 32px rows.
- Only the page in front is drawn selected, with its close button.
- While the top row is not in front, it ends in a 13px split icon at 55%, with the tooltip "Side by side". Under the pointer the close button takes its place.
- A pinned pair keeps Chromium's look.
- Built differently from the design: the pointer lights both rows of a pair, as Chromium's split does, and both show their close buttons.

### Mouse actions

Agreed on 2026-10-16 as T58. Each is on by default and has a switch in a new "Mouse" section of uw's settings page. A switch turned off leaves that click as Chromium has it.

| Action | Result |
| --- | --- |
| Option-click a tab's close button | Closes its branch, like Close branch. |
| Hover a task's folder row | Shows a "+" before the task's menu button. A click opens the [command bar](command-bar.md#opening-it) for the task. Tooltip "New tab in task". With the command bar switched off, a click opens a blank tab at the end of the task and selects it. |
| Double-click empty space below the tabs | Does what the foot's "+" does. The space still drags the window, but a double-click there no longer zooms it, as in Arc. |
| Middle-click a task's folder row | Closes the task and its tabs, like Close task. |

- The task row's "+" opens the command bar since it landed. Agreed on 2026-10-21.
- Elsewhere in the window, a double-click on space that drags the window still does what macOS's "Double-click a window's title bar" setting says. A double-click below the tabs did too until T58.
- The design is the frames "Mouse actions: the sidebar", "Mouse actions: settings", and "Mouse actions: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-16. The user signed off on 2026-10-19.

### Key hints

Agreed on 2026-10-30, when the user asked that holding Option on a tab's close button show that its children close too: "so users can discover features like this". The rule holds for every uw click or drag that a held key changes, now and later.

- While the pointer is on the target and the key is held, the target shows a [key hint](../CONTEXT.md#language): the other result, before the click.
- The hint follows the key. Pressing or releasing it shows or hides the hint at once, without moving the pointer. Switching to another app or window hides it.
- A mouse action switched off in uw's settings shows no hint, and its tooltip names no key.
- Where the target already has a tooltip, its tooltip names the key, so the hint can be found. [Help](help.md) names every one.

| Held on | Hint |
| --- | --- |
| Option, a tab's close button, when the tab has tabs under it | The tab's row and every row under it take a light tint of the close color, and the × turns that color. A collapsed parent's count of hidden tabs turns it too. The tooltip "Close tab" becomes "Close this tab and the 3 tabs under it". |
| Option, the rest of a parent tab's row | The rows under it fade to half, since a drag would move the tab alone. The row's tooltip reads "Drag to move this tab alone." and a second line, "The 3 tabs under it stay." An Option-drag keeps them faded until the drop. |
| Option, the floating sidebar's green window button | The full-screen arrows become a +, as macOS draws its own, and a click zooms the window. |
| Cmd, a link in a chat's answer | A label by the pointer: "Open in the background". |
| Cmd, an address a terminal tab printed | The address underlines and the pointer becomes a hand, as in Ghostty. Without Cmd it looks like text, since a plain click does not open it. |

Tooltips that name the key:

- A parent tab's close button: "Close tab" and a second line, "Option-click to close the 3 tabs under it too". A tab with nothing under it keeps the one line.
- A link in a chat's answer: "Cmd-click to open in the background".

The command bar's Cmd+Return and Option+Return are typed keys with labels of their own, and Chromium's own clicks with keys stay Chromium's.

The design is the frames "Key hints: Option on a tab's ×" and "Key hints: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-30 for T274 and signed off by the user the same day, the chat link's label included. Their rules are the "Key hints" section of the canvas guide `uw-keyboard-shortcuts`.

Built on 2026-10-30: the sidebar and the window buttons in T275, and chats and terminal tabs in T276. `MouseActionsBrowserTest.OptionOnAParentsCloseButtonTintsItsBranch`, `ALoneTabOrTheSwitchOffShowsNoHint`, and `OptionOnAParentsRowFadesTheTabsThatStay`, `UwChatBrowserTest.CmdOnAnAnswersLinkShowsWhatAClickDoes`, and `UwTerminalBrowserTest.AnAddressUnderlinesOnlyWhileCmdIsHeld` pass. `WindowScreenshotBrowserTest.OptionTurnsTheGreenButtonsArrowsIntoAPlus` checks the green button. The tests set the held keys directly. The × keeps Chromium's stroke width and changes only its color. Not tried: real Option and Cmd presses in a real window.

### Close button on a task's row

Agreed on 2026-10-31 as T284. Closing a task took two clicks from outside it: Ctrl+Cmd+W closes only the selected tab's task, and middle-click needs a mouse.

- An × at the right end of a task's row, in line with the tabs' close buttons, after the "+" and the menu button. It shows when they do: while the pointer is on the row, while the task's menu is open, or while the task holds the selected tab.
- A click runs Close task, as the task's menu, Ctrl+Cmd+W, and middle-click do. Those stay. A busy terminal in the task asks first, per the [busy terminal warning](terminal-tab.md#busy-terminal-warning).
- While the pointer is on the ×, the task's row and every tab row in it take the close color at 10%, as a [key hint](#key-hints) does. A collapsed task tints its row alone.
- Tooltip: "Close task and its 4 tabs", or "Close task and its tab" for one.
- The narrow sidebar and horizontal tabs have no ×. It has no switch in settings.
- The design is the frames "Close task in one click: the ×", "Close task in one click: five ways", and "Close task in one click: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-31, with their rules in the canvas guide `uw-keyboard-shortcuts`. The user picked the × from the five ways the same day. The every-state frame waits for sign-off.

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
- A branch can hold every tab of its task. Chromium moves a task whose tabs all move as one piece, so a drag that moved such a branch within its task asked Chromium to move the task into itself, and uw crashed (T277). `TabStripModel::MoveTabsToIndexImpl` in `tab-tree.patch` moves tabs into a task one by one, as Chromium's `MoveTabsAndSetPropertiesImpl` does.
- uw hooks `TabDragHandlerImpl::ContinueDrag` to pass the pointer's sideways distance to `TabOrganizationController`. The page tree works out the depths each place allows, and `Adopt` takes the one asked for. `tab_indent` shows it while the drag runs.
- Chromium tears a tab off once the pointer leaves the window. `ContinueDrag` holds a pointer up to 96px left of the window, six levels of indent, at the window's edge, so pulling a tab out of its branch keeps it. Farther out, a tab still tears off to another window.
- Chromium's `TabCollectionAnimatingLayoutManager` slides every row to new bounds in 200ms but puts a dragged row straight at its target. In a vertical strip uw slides a dragged row's x and width too.
- Chromium reverts a cancelled drag itself, from `TabDragController::EndDrag`, which tells `TabOrganizationController` first. The tree then goes back to how it was when the drag began.
- During a drag the page tree takes a dropped tab's ancestors from the tree when the drag began, so a branch keeps its shape through Chromium's steps, and a tab dragged back keeps its old parent.
- `TabOrganizationController` keeps which parents are collapsed and saves it with the session data that already carries parent links. Collapsing is not an organization action, so it is not in the undo history.
- Both vertical layouts that `tab-tree.patch` changes give a hidden row no height. The tab row places the chevron and the count from `browser/ui/views/tab_organization/branch_toggle`.
- `TabStripModel::IsTabCollapsed` also asks `HiddenTabs`, which the controller provides through the window. Ctrl+Tab and the choice of the next tab after a close already skip collapsed groups through it.
- The mouse switches are profile prefs read by the views they change.
- The empty space below the tabs drags the window, so macOS, not the views, gets its clicks. While macOS hands out the second click of a double-click, the space counts as the views' instead, and Chromium's double-click-to-zoom never sees it.
- "New child tab" is a `TabOrganizationController::Command`. The page tree places the new tab as the first child.
- Key hints: macOS sends a lone Option or Cmd press as a modifier change, not a key event, so `ModifierKeys` watches modifier changes with one AppKit event monitor for the app and lets go of both keys when uw leaves the front. Each tab row has a `KeyHint` view over it, added by `tab-tree.patch`'s hooks, which tints or fades the row and keeps the tooltips. It fades the row's parts, since Chromium's layout gives a row a layer of its own to animate it and then destroys it. Chromium's row shows hover cards, not tooltips, so the parent row's Option tooltip is the hint view's. The floating window buttons repaint their glyph. The chat page and terminal tabs read Cmd from their own key and mouse events.
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
14. T274: the [key hints](#key-hints) design.
15. T275: key hints in the sidebar and on the window buttons, with the key watcher. Implementability 5/10: the × is Chromium's, and one row's hover tints other rows.
16. T276: key hints in the chat page and terminal tabs. Implementability 7/10.
17. T284: the [close button on a task's row](#close-button-on-a-tasks-row), with T36, the busy terminal warning. Design first. Implementability 8/10 for the button; T36 is 5/10.

T73, T74, and the command bar are built next, then the other stages. Changed on 2026-10-15. T54 is built in the command bar's order, before T82, which needs it.

## Acceptance checks

1. Each key in the [table](#keys) runs its command in a headless window through a real key event. With a terminal tab selected it is checked by hand: a headless window sends a key the focused page left alone back into the page, not to the browser.
2. The Tab and Tasks menus list every command with its key. Help → Search finds "Move to Scratchpad".
3. In a task with a terminal tab, Ctrl+\` selects it and a second press returns to the page. In a task without one, Ctrl+\` opens one below the active tab's branch. Ctrl+Shift+\` opens a second one.
4. Dragging a parent moves its branch with its nesting intact. Option-drag moves the parent alone and its children move up one level.
5. [Sideways drag](#sideways-drag):
   - A child dragged below the last child of its branch and moved left lands at its task's top level.
   - A straight drag keeps the tab's parent inside that parent's branch. Outside it, the tab takes the level of the tab below, or the top level when nothing is below. It never becomes the child of a tab it passes.
   - Between two siblings, the tab can be their sibling or a child of the tab above, never shallower. In a task's last place it can reach from the top level to one deeper than the tab above.
   - A wobbly vertical drag keeps the depth. A drag goes no deeper than 6 levels.
   - The place below a collapsed parent goes no deeper than the parent.
   - Several selected tabs land as siblings at the depth shown, each with its branch.
   - A dragged parent's branch keeps its shape, moved up, down, or sideways. Its deepest tab goes no deeper than 6 levels.
   - A drop that gives a tab a new parent nests it by hand. A reorder inside the same branch does not, and dragging the tab out ends it.
   - Sideways never changes the task. Dragging left past the sidebar's edge does not tear the tab off.
   - Esc during a drag puts the tab back at its place and depth.
   - With "Drag sideways to nest tabs" off, sideways does nothing and a straight drag still keeps its parent as above.
   - A window screenshot shows the indent following the pointer, as in the signed-off frames, and the dragged row slides to a new depth.
   - `PageTreeTest` checks the depth rules, and a headless controller test checks nesting by hand. The mouse drags are `MANUAL_` tests in `BranchDragBrowserTest`, run with `pnpm run window-tests`.
6. A collapsed branch hides its children and shows their count. Ctrl+Tab skips them. Cmd+1 to 9 onto a hidden tab expands the branch. Closing the collapsed parent shows its children one level up. The collapse survives a restart, and an organizer move leaves it collapsed.
7. Each mouse action works, and does nothing once its switch is off.
8. Ctrl+Cmd+L focuses the tab list. The arrow keys walk the tree as described, and Esc returns focus to the page. The accessibility tree gives each tab row its level.
9. "New child tab" on a tab that is not selected and has children opens a selected blank tab directly under it, above those children. Ctrl+Cmd+T does the same for the selected tab. "New tab below" on a tab that is not selected opens below that tab's branch.
10. A child nested by hand moves with its parent when the organizer moves the branch, and is not filed alone when its page changes. A child opened from a link still can be. The hold survives a restart and ends when the parent closes.
11. A tab's right-click menu lists its items in the order of [Tab menu](#tab-menu), with no "Add tab to new group" or "Remove from group".
12. A click on the foot's "+" does what Cmd+T does, "▾" lists its five items with their keys, and the tab list has no "+".
13. Ctrl+Cmd+N opens a new task, Shift+Ctrl+Cmd+N moves the selected branch to a new task, and Ctrl+Cmd+P does nothing.
14. The Keyboard shortcuts section lists uw's and Chromium's keys by menu. A new key for a uw command runs it in a headless window, after a restart too, and in every profile. A key without Cmd is refused. Taking Cmd+G moves it from Find Next, whose row then reads "No key". Reset and Reset all bring back the defaults. `MenuKeysBrowserTest` and `UwShortcutsPageBrowserTest` check this.
15. Ctrl+Cmd+K keeps the selected tab and a second press lets it go. Keep here is in the tab's right-click menu and the Tasks menu.

16. [Key hints](#key-hints): with Option held on a parent's ×, the branch's rows tint and the tooltip names the count. Releasing Option clears it without moving the pointer, and so does switching apps. A tab with no children, and an action switched off, show nothing. The rows under a parent fade with Option held elsewhere on its row. The floating green button shows a + with Option held. A chat link shows "Open in the background" with Cmd held, and a terminal address underlines only with Cmd held.
17. [Close button on a task's row](#close-button-on-a-tasks-row): with a Scratchpad tab selected, the pointer on a task's row shows its ×, and a click closes the task and its tabs. With the pointer on the ×, the task's rows tint and the tooltip counts its tabs. Cmd+Shift+T brings the task back. A task with a busy terminal asks first.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Whether window manager apps on the user's Mac already take Ctrl+Cmd with the arrows. Check by hand before T53 ships.
- Where Ctrl+\` returns to when the tab selected before the terminal has closed. Proposal: the tab above the terminal.
