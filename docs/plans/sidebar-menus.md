# Sidebar menus

Status: scope agreed on 2026-10-17, and the designs, T113, drawn the same day for sign-off. Not built.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Naming a task already runs through the language model, and the activity card already reports failures. The risk is editing a name on Chromium's task row and putting uw's card where Chromium opens its group editor without breaking what Chromium still uses that editor for. Each Chromium update can move the views both touch.

The task menu and the Organize menu become uw cards in the [Arc-style look](arc-style-look.md), instead of Chromium's group editor and a plain macOS menu. A task is named on its own row.

## Agreed scope

### Both menus

- A card in the theme picker's family: white in light mode, `#171717` with a `#262626` ring in black dark mode, a 14px radius, 30px rows, uw's line icons at 16px, short title-case labels, and keys in the muted color at the right.
- Built as native views, not WebUI pages, so they open at once and work with the keyboard and VoiceOver as Chromium's bubbles do. The arrow keys move between rows, Return runs one, and Esc closes the card.

### Task menu

- The ⋮ button on a task's row and a right-click on the row open the same card, under the row. This is in the sidebar only; horizontal tabs keep Chromium's group editor.
- Rows, from the top:
  1. Rename Task, and Name Task while a language model is set.
  2. The task's color as a row of small dots, the current one ringed in the primary color.
  3. New Tab in Task and Move Task to New Window.
  4. Move Tabs to Scratchpad and Close Task.

### Naming in place

- Double-clicking a task's name, Rename Task in its card, and Tasks → Rename Task in the menu bar each turn the name on the row into a text field. A task the user creates, with Ctrl+Cmd+N or New Task, starts that way. Return or a click elsewhere saves, and Esc puts the old name back.
- In a collapsed sidebar, renaming widens the sidebar until the name is saved or dropped.
- Name Task closes the card at once and asks the language model in the background:
  - The task's row shows its spinner, and the name lands on the row when it comes. No window has to stay open.
  - On failure the old name stays, and the activity card says "Couldn't name" with the task, the reason, and Try Again. It shows even with Show Organizer Activity off, because the user asked.
  - If the user starts renaming while it runs, the typed name wins and the model's answer is dropped.
- "Name Task" replaces "Suggest a Name" everywhere: the card, the Tasks menu, and later the command bar.

### Organize menu

- The card opens upward from the Organize button. From the top:
  1. A status line with a dot:

     | State | Status line | Pause row |
     | --- | --- | --- |
     | On | ● Organizing | Pause, with the chips `1 hour`, `Until restart`, and `Until I resume` |
     | Paused for an hour | ● Paused, resuming in 1 hour, counting down to "in 1 min" | Resume |
     | Paused until restart | ● Paused until uw restarts | Resume |
     | Paused until resumed | ● Paused | Resume |
     | Off | ○ Off | `Turn On` |
     | Waiting for a model | ○ Needs a model | `Set Up`, which opens AI providers |

     The dot is green when on, amber when paused, and gray otherwise. The words carry the state, not the color alone.
  2. The pause row from the table.
  3. Undo Move with ⌥⌘Z, and the move it undoes below it in muted text, such as "Hotels in Baixa → Plan Lisbon trip". With nothing to undo, it is grayed and says "Nothing to undo".
  4. Show Activity, with a switch.
  5. Settings….
- New Task leaves this menu. The new button's menu and Ctrl+Cmd+N have it.

## Design

On the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Sidebar menus", drawn on 2026-10-17:

- "Task menu: the card": the card open under a task's row.
- "Organize menu: the card": the card over the Organize button, paused with 42 minutes left.
- "Sidebar menus: every state": renaming on the row, a new task, Name Task asking and failed, the Organize menu organizing, paused until restart, off, and waiting for a model, and both cards in dark mode.

The rules, with every size and color, are in the `uw-menus` guide. Two details the designs added: both cards are 264px wide so the pause chips fit, and Undo Move cuts the tab's title before the task's name.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T113: the designs, signed off by the user.
2. T114: the task menu card.
3. T115: naming in place and Name Task in the background.
4. T116: the Organize menu card.

## Acceptance checks

1. The ⋮ button and a right-click on a task's row open the same card under the row. Arrow keys move between rows, Return runs one, and Esc closes it.
2. A double-click on a task's name makes it a field on the row. Return and a click elsewhere save it, and Esc puts the old name back. Ctrl+Cmd+N opens a task with its name field ready.
3. Name Task closes the card, the row spins, and the name arrives with no window open. A failed request shows in the activity card with Try Again. A name typed while the model works is kept.
4. The Organize card shows the right status line in each of the six states, and the countdown follows the time. Each chip pauses for its time, and Resume ends the pause.
5. Undo Move names the move it undoes, and is grayed with "Nothing to undo" when there is none.
6. With horizontal tabs, a task still opens Chromium's group editor.
7. Headless screenshots of both cards and of a row being renamed match the frames in light and dark mode.
8. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

None.
