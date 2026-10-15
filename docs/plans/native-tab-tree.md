# Native tab tree: tasks as tab groups

Status: approved on 2026-10-11. This replaces the first slice, which drew uw-owned task containers inside Chromium's tab container. That code is kept in commit `f113a96` and is deleted as each stage below lands.

Implementability: **7/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium already draws, drags, renames, collapses, and restores tab groups. The remaining risk is keeping page nesting consistent with native drags and session restore.

The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Tab tree". The decision is recorded in [ADR 0002](../adr/0002-tasks-are-native-tab-groups.md).

## Scope

- A task is a native Chromium tab group. Chromium supplies its header, collapse, rename, color, drag, and session restore.
- Ungrouped regular tabs are the [Scratchpad](../product.md#scratchpad). They carry a "Scratchpad" label and stay below every task. Changed on 2026-10-13.
- Tasks are listed in the order they were started. A new task comes after every other task. Agreed on 2026-10-13.
- A link opened in a new tab becomes a child of its source tab. A blank new tab opens as a sibling below the active tab's branch. "New tab below" and "New child tab" in a tab's right-click menu go by that tab: see [New child tab](keyboard-and-mouse.md#new-child-tab).
- Child tabs are indented 16px per level and follow their parent in tab order. A child lives in its parent's task.
- Closing a tab moves its children up one level. "Close branch" closes the tab and everything under it.
- Tab menu commands: "New child tab", "Move to new task", "Move to Scratchpad", "Nest under previous tab", "Move up one level", "Close branch".
- An "Organize" button at the foot of the sidebar opens a menu: "New task", "Undo tab organization", the pause controls, "Show organizer activity", and the settings page.
- "New task" opens a new tab in a new task of its own, named `New task`. The sidebar's "+" button and Ctrl+Cmd+N do the same. The key was Chromium's Ctrl+Cmd+P until 2026-10-15; see [Keys](keyboard-and-mouse.md#keys). Cmd-T still opens a tab beside the active tab, in its task. Agreed on 2026-10-12.
- [Organization undo](../product.md#organization-undo) reverses one organization action and keeps tabs opened since.
- Option+Cmd+Z runs "Undo tab organization" in the focused window. Each press goes back one action, the user's or uw's, whether or not the activity card is showing. Nothing extra is shown. The Organize menu shows the key beside the item. Agreed on 2026-10-13.
- Parent links survive session restore.

## Non-goals

- Tasks inside tasks.
- Nesting a tab by dragging it. Changed on 2026-10-14: the [keyboard and mouse plan](keyboard-and-mouse.md#branches-with-the-mouse) adds it.
- Moving a branch between windows as one unit.
- Shared live pins. They stay in the later pin work.

## Design

- `components/tab_organization/page_tree` is a value type: tab order, task key, and parent link for each page of one window. Every rule lives there as plain functions, so it is tested without a browser.
- `browser/tab_organization/tab_organization_controller` is owned by the window. It mirrors `TabStripModel` into a page tree, and applies tree changes back through Chromium's tab-group and move calls. It never changes the tab strip from inside an observer notification.
- When the user changes tabs natively, the controller adopts the new order. A parent link survives only while the child still sits inside its parent's branch.
- uw adds three things to Chromium's vertical tab strip: indentation, the Scratchpad label, and the Organize bar at its foot. The bar holds the Organize button and the [automatic-move message](automatic-organization.md#agreed-scope). No uw view replaces or wraps a Chromium container.
- Chromium edits stay in `chromium/patches/` as small hooks.
- The [Arc-style look](../product.md#minimum-usable-version) is separate work: it restyles Chromium's own views and does not change this structure.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. Page tree model with unit tests.
2. Window controller on native tab groups. Deletes the old tree model, coordinator, and adapter.
3. Sidebar additions: indentation, Scratchpad label, Organize button.

Automatic organization builds on these stages. See [Automatic organization](automatic-organization.md).

## Acceptance checks

1. Ungrouped tabs show under "Scratchpad", below every task. `TabOrganizationControllerBrowserTest.ScratchpadLabelSitsAboveUngroupedTabs` checks where the label sits.
2. A link opened in a new tab is indented under its source. A blank new tab appears directly below the active tab's branch at the same level.
3. Closing a parent keeps its children open, one level up, in the same order.
4. "Move to new task" moves the tab and its descendants into a new tab group placed after every other task. `TabOrganizationControllerBrowserTest.ANewTaskComesAfterEveryOtherTask` checks it.
5. "Move to Scratchpad" ungroups the branch, detaches it from its parent, and puts it at the end of the Scratchpad.
6. Dragging a child out of its parent's branch removes the indentation. Dragging a tab never changes another tab's task.
7. One undo reverses a move or task creation as a whole. Tabs opened since stay open.
8. Collapse, rename, color, and drag of a task behave exactly as they do in stock Chromium.
9. Indentation and task membership survive restart with session restore.
10. Switching to horizontal tabs and back keeps tasks and parent links.
11. Option+Cmd+Z undoes one organization action per press. `UndoShortcutBrowserTest.OptionCmdZUndoesOneOrganizationActionPerPress` checks it.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`.
