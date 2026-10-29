# Tab tools

Status: scope agreed on 2026-10-29 in a grilling session. Nothing is built.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). The tab tree already moves and closes tabs as one undoable action, page ids already outlast a restart, and chats, the [`uw` command](uw-command.md), and `uw mcp serve` already add tools the same way. The risks: [shared tabs](shared-tabs.md) are being built at the same time, a new kind of approval card has to show in four places, and the tools reach every window of a profile while a chat's tools today hang off its own tab.

The user asked on 2026-10-29 for tab management tools in `uw://chat` and the same tools in the `uw` command. Until now the agent could use only [its own tabs](../CONTEXT.md#language), although the [agent chats scope](agent-chats.md#agreed-scope) lets it use the titles and addresses of open tabs.

## Agreed scope

### The tools

| Tool | What it does | `uw tabs` |
| --- | --- | --- |
| `tabs` | Lists the user's tabs, by workspace and task. | `list` |
| `select` | Shows a tab. | `select <tab>` |
| `move` | Moves tabs to a task, to a new task with a name, or to the Scratchpad. | `move <tab>... --task <task> \| --new-task "Name" \| --scratchpad` |
| `rename_task` | Renames a task. | `rename-task <task> "Name"` |
| `close` | Closes tabs, each with the tabs nested under it. | `close <tab>...` |
| `close_task` | Closes a task and its tabs. | `close-task <task>` |

Later: nesting and outdenting, keeping, collapsing, Tidy and Clear, pins, and moving tabs to another workspace.

### Which tabs

- Every workspace of the chat's [profile](../CONTEXT.md#language), hidden windows included, as the command bar's open-tab rows are. A private chat reaches only private windows, and other chats never reach them. The run window is never included.
- [Pinned tabs](../CONTEXT.md#language) are listed and can be selected. `move` and `close` refuse them.
- The agent's own tabs are listed too, with the number the agent knows them by.

### Arrange only

The tools see each tab's title, address, task, place in the tree, and when the user last looked at it. They never see page text. `read`, `click`, and `type` still work only in the agent's own tabs. To read one of the user's pages, the agent opens its address in a tab of its own. Working in a tab of the user's stays with "giving the agent a tab", in v2.1 of the [agent chats plan](agent-chats.md#releases).

### Approvals

| Tool | Ask first | Auto | Full auto |
| --- | --- | --- | --- |
| `tabs`, `select` | no | no | no |
| `move`, `rename_task` | asks | no | no |
| `close`, `close_task` | asks | asks | no |

- One card covers one call, such as "Close 7 tabs?", with each tab's icon and title, at most 5 and then "and 2 more".
- The agent closes its own tabs without asking.
- `close` and `close_task` refuse a [busy terminal](../CONTEXT.md#language) and say so, as Clear does. A kept tab can be closed.

### Naming tabs and tasks

- A tab is named by its page id, which the tab tree saves in Chromium's session file. It is printed as its first 8 characters, such as `3f9a1c07`, and any unique start of it works, as with chat ids. An ambiguous one is an error.
- A task is named the same way, by its id in the tab tree. Task names are not unique: every new task is called `New task` at first.
- The agent's own tabs show both, such as `3f9a1c07 (your tab 2)`, and the tab tools take either.

### The agent acts for the user

- A move is a move by hand: it holds its branch from the organizer, and each `move` call is one organization action that Undo Tab Organization reverses.
- A name the agent gives counts as typed by the user: it shows the name lock, and uw never replaces it. Like a rename by hand, undo leaves it alone.
- Tabs move only within their workspace. `move` refuses tabs from another workspace than the target task's. A new task starts in the tabs' workspace.

### Workspaces and windows

The tools speak of workspaces, tasks, and tabs, not windows, since every window on a workspace shows one tree with [shared tabs](shared-tabs.md#sharing). While the `UwSharedTabs` switch is off, a workspace with several windows lists each window's tree on its own, as "Work (window 2)", and every rule above applies to each tree.

### The list

- One line per tab: id, title, site, and flags such as selected, kept, pinned, busy, and when the user last looked at it, such as "3 days ago". Workspaces and tasks are headings:

  ```
  Work › Flights (task 9c2e41ab)
    3f9a1c07  Flights to Lisbon · google.com · selected
    a81d0f3e  TAP Air Portugal · flytap.com · 3 days ago
  Work › Scratchpad
    …
  ```

- `tabs` takes `query`, words in a title or address, and `workspace` and `task`.
- It lists at most 300 tabs, then says how many more and to narrow with `query`.
- An older `tabs` answer shrinks to one line once a newer one arrives, as a page reading does.
- The agent's instructions carry no list of tabs. It sees them only when it calls `tabs`.

### Selecting

- `select` shows the tab in the front window on its workspace, which takes the live page. When no window in view shows the workspace, the front window switches to it, as picking an open tab in the command bar does. uw does not come in front of other apps.
- Selecting a tab in the chat's own window leaves the chat page; the chat keeps working.
- `uw tabs select` also brings uw forward, as `uw connectors sign-in` does.

### Who gets the tools

| Caller | Tools | Approvals |
| --- | --- | --- |
| A chat, also from `uw chat` | all six | the chat's mode |
| A [run](../CONTEXT.md#language) of a scheduled chat, and a trigger's wake | all but `select` | the chat's mode; a close in Auto waits with its notification |
| `uw mcp serve` | all but `select` | the client's mode, as for the page tools |
| `uw tabs` | all six, run directly | none |

- `uw tabs` needs no approval: the user or their script typed it, as with `uw settings set`. Approvals keep page text from steering the agent; they do not guard against programs the user runs, per the [uw command plan](uw-command.md#approvals).
- `uw tabs` takes `--json` and `--workspace`, with the usual exit codes: 0 done, 1 failed, 2 wrong usage. `list --json` prints the objects the `tabs` tool answers with. `--workspace` defaults to the workspace used last, and `list` shows only that one, but ids work anywhere in its profile.

### In the chat

- Step lines: "Listed 34 tabs in 2 workspaces", "Switched to **Flights to Lisbon**", "Moved 4 tabs to **Flights**", "Moved 2 tabs to a new task, **Hotels**", "Moved 3 tabs to the Scratchpad", "Renamed **New task** to **Trip to Lisbon**", "Closed 7 tabs in **Flights**", and "Closed the task **Flights** and its 7 tabs". Each line opens to the tabs' titles.
- The approval card's label is "Your tabs · Work" and its question "Close 7 tabs?" or "Move 4 tabs to Flights?", with the usual choices. `uw chat` and `uw mcp serve` ask in the same words.
- A step has no Undo button. Undo Tab Organization reverses a move, and Recently Closed brings a closed tab back.

## Design

T267 draws one frame on the "Agent chats" page of the [canvas](../../AGENTS.md#designs): the step lines, the close and move cards, and `uw tabs list` in the [`uw` command's](uw-command.md#design) terminal look. Nothing is built before the user signs it off.

## How it is built

Proposed. Product code follows [Architecture](../architecture.md#code-organization).

- `components/agent/agent_protocol.cc`: the six tools' schemas beside the page tools.
- `browser/agent/tab_tools.h`: `TabTools` lists and changes a profile's tabs through each tree's `TabOrganizationController`: `MoveToTask`, `MoveToNewTask`, and `MoveToScratchpad` as the user's moves, `CloseBranches`, and `CloseTask`. Chats, runs, and the command all call it.
- `AgentTools::Call` passes the tab tools to `TabTools`, and `ApprovalFor` adds their cards. A run's tools leave out `select`.
- `browser/uw_command/tabs_command.cc` answers `uw tabs`, and `tools_command.cc` adds five tools to `kPageTools`.
- `command/src/tabs.ts` is `uw tabs`. `steps.ts` and the chat page's `main.tsx` word the step lines.

## Stages

Each stage is a ticket on the [board](roadmap.md#board), and each one that users see updates its [Help](help.md) page in the same change.

1. T267: the design, signed off by the user.
2. T268: the tools in chats and runs, with their approvals and step lines.
3. T269: `uw tabs`.
4. T270: the tools in `uw mcp serve`.

## Acceptance checks

1. Asked "close my Scratchpad tabs about flights", a chat lists the tabs, asks once in Auto with each title, and closes only those after Approve. Not this one closes nothing.
2. `tabs` lists every workspace of the profile, hidden windows included, and leaves out private windows and the run window. A private chat sees only private windows.
3. `move` to a new task starts it in the tabs' workspace with the agent's name and the name lock. One Undo Tab Organization puts the tabs back. The organizer leaves the moved branch alone.
4. `close` refuses a busy terminal and a pin, and `move` refuses tabs from two workspaces, each with a reason the model reads.
5. A tab's 8-character id from `uw tabs list` still works with `uw tabs close` after uw restarts.
6. `uw tabs move a81d0f3e --new-task "Hotels"` moves the tab without asking and exits with 0. A wrong id exits with 2.
7. A scheduled run that closes tabs in Auto waits and sends its notification, and has no `select`.
8. With `UwSharedTabs` on, two windows on one workspace list each tab once, and with it off, they list two trees.
9. `pnpm run check` and the focused tests pass. Browser tests stay headless.

## Open questions

None.
