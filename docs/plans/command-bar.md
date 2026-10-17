# Command bar

Status: scope agreed on 2026-10-14. Widened on 2026-10-15: one list ranked by use, instant results only, sending the typed text to a chat or a terminal, and sums with units and currencies. T60, the spike, started on 2026-10-15 at the user's request. The other tickets wait for approval. The command bar runs the commands of the [keyboard and mouse plan](keyboard-and-mouse.md).

Implementability, using the [roadmap convention](roadmap.md#implementability-estimates): **6/10** for the command and task rows (T61), and **4/10** for ranking by picks (T111). Taking over Cmd+L and clicks on the address means the command bar must keep what people rely on in Chromium's address bar. One ranked list has to put uw's commands and tasks on the same scale as Chromium's history and bookmark scores. Reusing Chromium's suggestion engine keeps history, bookmarks, and search from being rebuilt. The later tickets: sending to a chat 7/10, running in a terminal 6/10, aliases and keys 6/10, and the calculator 6/10.

The decision to replace typing in the address bar is recorded in [ADR 0005](../adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md). The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Command bar": the frames "Command bar: as you type", "Command bar: before typing", "Command bar: every state", "Calculator: units and currencies", "Calculator: every state", "Chat: send the text with Cmd+Return", "Terminal: run the text with Option+Return", and "Chat and terminal: every state". Redrawn on 2026-10-15 for the wider scope. The look's rules are in the canvas style guide `uw-command-bar`. The user has not signed off yet.

## Agreed scope

### Opening it

- The [command bar](../CONTEXT.md#language) is a panel centered over the page card. The page dims a little behind it.
- Cmd+T and File → New Tab open it to open a new page. Escape creates nothing. A page picked from it opens where a blank new tab opens today, below the current tab's branch.
- "New child tab" and Ctrl+Cmd+T open it too. A page picked from it opens as the first child of that tab, per [New child tab](keyboard-and-mouse.md#new-child-tab). Agreed on 2026-10-15.
- A "Command bar" section on uw's settings page has a switch, "Cmd+T opens the command bar". It is on by default and takes effect at once in every window. Off, Cmd+T and File → New Tab open a blank tab. Until T98 the command bar cannot open pages, and the section says so. Agreed on 2026-10-15.
- Cmd+L, and a click on the address in the toolbar, open it filled with the current address, selected. A page picked from it loads in the current tab. The lock, star, and zoom icons beside the address keep their own clicks.
- Cmd+Shift+A opens it narrowed to open and recently closed tabs. Chromium's tab search popup and its toolbar button are hidden.
- Cmd+Shift+P opens it with `>` in the field, ready for a shell command. See [Sending the text to a chat or a terminal](#sending-the-text-to-a-chat-or-a-terminal). Agreed on 2026-10-15.
- Commands run on the tab that was selected when the command bar opened.
- It opens at once and never loses a key. Every typed address goes through it, so a late or dropped key breaks the [primary design rule](../product.md#principles). Keys pressed between Cmd+T and the panel appearing land in its field, in order. Added on 2026-10-14.
- A "New blank tab" command opens Chromium's new tab page.

### What it finds

- Commands, from the list the [keyboard and mouse plan](keyboard-and-mouse.md#design) defines, and Chromium's own browser commands.
- Tasks, to go to one or to move a tab there.
- Open tabs, history, bookmarks, and past searches, from the same suggestion engine as Chromium's address bar. Chromium's site search shortcuts, such as `gh` and Tab, work as they do there. An address being typed is completed inline, as the address bar does.
- Only results that are ready at once. The list changes only when the user types, never because a result arrived late. Changed on 2026-10-15:
  - Live suggestions from the search engine are left out, so nothing typed leaves the Mac until the user picks a row.
  - History covers the pages Chromium's address bar can find at once: pages typed at least once, visited 4 or more times, or visited in the last 3 days.
- Before anything is typed, the tabs, tasks, commands, and pages the user picks most often and most recently. Cmd+L shows the current address instead. Changed on 2026-10-15: before, the most recent tabs and commands.

### Ranking

Agreed on 2026-10-15, after [Raycast's ranking](https://www.raycast.com/changelog/macos/1-40-0). Before, the best match came first and fixed sections followed.

- One list, ranked as a whole. Each row has a small label on its right: Tab, Task, Command, Bookmark, History, or Search.
- A row rises the more often and the more recently it is picked from the command bar. It rises further when the typed text, or the start of it, was typed before that row was picked.
- Only picks in the command bar count. A command run from its key or a menu does not. Chromium's history rows also keep Chromium's own ranking by visits.
- A math answer comes first when the text is math. When nothing matches well, the last rows are "Search the web for …" and "Ask uw …".
- Cmd+K on a row offers "Reset ranking", which forgets what uw learned about that row.
- What uw learns stays on the Mac, in the profile. Private windows teach it nothing. Clearing browsing history clears it.

### Picking a row

- Return runs the selected row's first action. Cmd+K opens its [result menu](../CONTEXT.md#language): a tab can be switched to, closed, moved to a task, or have its link copied.
- "Ask uw …" does what Cmd+Return does.

### Sending the text to a chat or a terminal

Agreed on 2026-10-15. The user types first and picks where the text goes with the key that sends it.

| Key | What it does |
| --- | --- |
| Return | Runs the selected row. |
| Cmd+Return | Starts a new [chat](../CONTEXT.md#language) with the text as its first message. The tab that was selected is handed to the agent as its tab 1: the agent is told it is the page the user asked from, reads it first, and can act in it with the usual approvals. The tab stays where it is. The chat opens in front, where a blank new tab would. Changed on 2026-10-16: before, the tab was only attached. |
| Option+Return | Runs the text as a shell command in a new terminal tab, which opens in front where a blank new tab would. Changed on 2026-10-16: before, in this task's terminal. |
| Tab | Turns the completion after the caret into typed text, as in Chromium's address bar. With no completion, on a site search keyword such as `gh`, it starts that site search. |

- Cmd+K on the text offers "Add to <chat>" for a chat that is already open, "Ask without this tab", and "Ask in the background", which opens the chat without selecting it.
- Cmd+K on the text offers "Run in a new terminal" and "Run and stay here", which runs it without leaving the current tab.
- A `>` at the start of the field marks a shell command, and Return runs it as Option+Return does. Cmd+Shift+P puts it there. Typing `>` in an empty field does the same, and Backspace in an empty field removes it.
- Ctrl+Return keeps Chromium's meaning: it adds `www.` and `.com` to the typed word and opens it.
- An address is completed inline after the caret, as in Chromium's address bar, and Return opens the completed address. Chromium completes an address with a path once it was typed twice, and a bare host once. Added on 2026-10-16.

### Models

- Text typed in the command bar reaches a model only when the user asks for one: a translation row, Ask uw, or Cmd+Return. Nothing is sent while typing.

### Aliases and recorded keys

- An alias is a short word for a command, such as `mt` for Move to task. Short words for pages stay Chromium's site search shortcuts.
- The user can record a key for any command from its result menu. It works while a uw window is in front, and nowhere else.
- A recorded key must include Cmd, so it works in a terminal tab. uw refuses a key already in use and names the command that has it.

### Math, units, currency, and translation

Widened on 2026-10-15. Before, unit conversions were a non-goal.

- Math, units, and currencies are worked out on the Mac and shown as the first row. Return copies the result.
- The answer shows when the text has a number, is not only a number, and mathjs reads all of it apart from an unfinished end: `5 * pi` answers, and `pi`, `cup`, and `2024` show nothing. A no-answer card shows only when the text also has an operator or "to", "in", or "as", and a unit mathjs knows: `1/2 cup flour` says flour is not a unit. Other text gets no card, so `covid-19` and `iphone 15 pro` stay searches. Agreed on 2026-10-17.
- Units and currencies mix in one sum: `10 HKD + 10 EUR to USD`, or `20 kg * 10 m/s/s / 5W`, which shows 40 s/m.
- The answer is in mathjs's units, which combine base units into named ones, so kg·m/s² becomes N. Typed units that multiply into a unit with its own name stay in it: `100 W * 3h` shows 300 Wh, and `2 A * 3 h` shows 6 Ah. `to N/W` converts to another unit. Changed on 2026-10-17: before, the typed units were kept.
- `min` is minutes, unless `(` follows it.
- Units run together read as their product, as `Wh` does: `Wm` is W·m, and `Nm` is N·m. A sum reads left to right, so `200 N / 5 Wm/s` shows 40 m⁻², and `200 N / (5 Wm/s)` shows 40 s²/m². Agreed on 2026-10-17.
- A half-typed sum shows the answer for the part that is complete: `10m *` shows 10 m.
- The answer shows beside the sum as mathjs read it, so the user can check it against what they meant. It is not tidied, only spelled with × and ÷: `10 m/s/s` shows 10 m ÷ s ÷ s. Under it, the units it read are named in words, such as "meters, seconds". A name that is not a unit or currency gives no answer, and the card says so; only an unfinished end is set aside. Agreed on 2026-10-17.
- A bare amount converts: a unit to the other measurement system, so `10 kg` shows pounds, and a currency to the default currency.
- Currency uses the European Central Bank's daily reference rates, fetched at most once a day. Fetching them sends nothing the user typed. Offline, the last rates fetched are used and their date is shown.
- The default currency is the currency of the Mac's region. A "Default currency" setting in the Command bar section of uw's settings page picks another. `in EUR` picks one for a single sum.
- A "Translate … to <browser language>" row appears when the text looks like another language, or after typing "translate". It asks the language model only when picked. The answer shows in the command bar with a Copy action. Split into T112 on 2026-10-17, after T66.

## Non-goals

- A plugin system. It is a possible later direction, not part of this plan.
- Hotkeys that work outside uw.
- Live suggestions from the search engine. Added on 2026-10-15.

## Design

- `browser/resources/uw_command_bar/` and `browser/ui/webui/uw_command_bar/` hold the page and its Mojo link, as the terminal's do. It shows in a top-chrome WebUI panel, the kind Chromium uses for its tab search popup.
- While the switch is on, each window keeps its command bar page loaded and shows it in a `WebUIBubbleDialogView` over the page card, from `browser/ui/views/command_bar/`. Chromium's `WebUIContentsPreloadManager` keeps one page for all of Chromium and picks which, and tab search's bubble drops its page 30 seconds after closing, so neither keeps it ready. Keys pressed before the page can take them are held by the browser and passed to the page in order once the page says its field has focus. Until the page's first frame is on screen, the browser refuses its keys, so a refused key stays held and is tried again a frame later. After one second the browser stops holding.
- Open tabs, history, bookmarks, past searches, and site search shortcuts come from Chromium's suggestion engine, asked for instant results only with `AutocompleteInput::set_omit_asynchronous_matches`, the mode Chromium uses for pasted text. The spike used the new tab page's searchbox handler (`chrome/browser/ui/webui/searchbox/`), which loads a picked page inside the panel. T98 subclasses it as `UwCommandBarHandler`: it opens a picked row in a new tab, runs an open-tab row's switch action, names each row's kind for the page, and asks for instant results only through a hook `uw-command-bar.patch` adds to Chromium's `SearchboxHandler`. The panel's `OpenURLFromTab` hands the page to the window, which opens it where `chrome::NewTab` would put a blank tab. The command bar adds uw's commands, tasks, fallbacks, math, currency, and translation beside them.
- Chromium's rows keep its scores. uw scores its own rows on the same scale and adds what it learned from picks to every row.
- Cmd+T, File → New Tab, Cmd+L, the address click, and Cmd+Shift+A are routed to the command bar where Chromium handles `IDC_NEW_TAB`, `IDC_FOCUS_LOCATION`, and `IDC_TAB_SEARCH`. Cmd+T is one of Chromium's reserved keys, so pages never receive it. Cmd+Shift+P is not used by Chromium on the Mac.
- What uw learns from picks, aliases, recorded keys, and the default currency are profile prefs. A history deletion clears what uw learned.
- Cmd+Return and Option+Return reach the browser over the searchbox connection the page already has: the page opens with the Cmd or Option flag set, and `UwCommandBarHandler` hands the query's text to its window. Cmd+Shift+P has the handler set the field to `>`, through the searchbox page's `SetInputText`.
- Cmd+Return opens `uw://chat` the way New chat does, and the chat page sends the text as if typed. The chat's agent adopts the selected tab as tab 1, as it adopts its own tabs after a restart.
- Option+Return writes the text and a Return to the shell through the one function every byte to the shell passes, per the [terminal tab design](terminal-tab.md#design).
- The math library is pinned to an exact version in `package.json`, like ghostty-web. mathjs 15.2.0, agreed on 2026-10-17. It has units and lets uw add each currency as a unit. The rates are fetched by the browser process.
- Translation is one chat request to the language model, through the connection automatic organization already uses.

## Stages

Each stage is a ticket on the [board](roadmap.md#board), built in this order. Changed on 2026-10-16: T99, T82, and T81 moved ahead of T61 at the user's request, and T82 no longer waits for T54. Changed again the same day: the user put commands off, so T66 comes before T61.

1. T60: spike. A preloaded top-chrome WebUI panel opens on Cmd+T without creating a tab, keeps every key typed right after Cmd+T, and lists suggestions from Chromium's searchbox handler.
2. T98: the command bar opens what is picked, in the design's look, from Chromium's instant results. Added on 2026-10-16 so Cmd+T opens pages before T61.
3. T99: addresses complete inline, Tab takes the completion, and Return opens the completed address. Added on 2026-10-16.
4. T82: Option+Return, `>`, and Cmd+Shift+P run the text in a new terminal.
5. T81: Cmd+Return sends the text to a new chat, with the selected tab handed to the agent.
6. T66: math, units, currency, and the default currency setting.
7. T112: translation. Split from T66 on 2026-10-17.
8. T61: command and task rows, with their keys and tab counts, and the task of each tab row. Split on 2026-10-16.
9. T111: one list ranked by picks, and the most-picked items before typing. Split from T61 on 2026-10-16.
10. T62: Cmd+L and the address click open it.
11. T63: the result menu, Reset ranking, the Cmd+K choices for chats and terminals, and the fallbacks.
12. T64: Cmd+Shift+A opens it narrowed to tabs.
13. T65: aliases and recorded keys.

## Acceptance checks

1. Keys sent right after Cmd+T, before the panel has drawn, all appear in its field in order, in 100 of 100 runs of a headless test. On the user's Mac the panel shows within 100 ms of Cmd+T. The 100 ms is uw's proposal.
2. Cmd+T opens the command bar over the selected tab, listing the most-used items. Escape leaves the tab strip unchanged. Picking an address opens a tab below the selected tab's branch.
3. Typing "move to scratchpad" and Return moves the tab that was selected.
4. Cmd+L shows the current address selected. Editing it and Return loads the new address in the same tab. A click on the address does the same. The lock icon still opens site information.
5. For the same text, the history, bookmark, and past search rows match Chromium's instant results, and a test search server receives no request while text is typed.
6. After a row is picked twice for "gi", typing "g" puts it first. Reset ranking puts it back. Picks in a private window change nothing.
7. Cmd+K on a tab row offers Switch, Close, Move to task, Copy link, and Reset ranking.
8. Text with no good match ends with "Search the web for …" and "Ask uw …".
9. Cmd+Shift+A shows open and recently closed tabs only.
10. Cmd+T, Cmd+L, Cmd+Shift+A, and Cmd+Shift+P work with a terminal tab selected.
11. A test model server receives no request while text is typed, and one request when a translation row is picked or Cmd+Return is pressed.
12. Cmd+Return opens a chat in front, with the text as its first message and the selected tab as the agent's tab 1, which the agent can read.
13. Option+Return runs the text in a new terminal and selects it. Cmd+Shift+P opens the command bar with `>`, and Return runs the text the same way.
14. An alias runs its command. A recorded key without Cmd is refused, a key in use is refused with its owner named, and a recorded key runs its command after a restart.
15. `2*(3+4)` shows `14` first. `100 usd in eur` uses the day's rate and shows its date. `10 HKD + 10 EUR to USD` adds both at the day's rates. `20 kg * 10 m/s/s / 5W` shows 40 s/m, `100 W * 3h` shows 300 Wh, `90 min to h` shows 1.5 h, and `10m *` shows 10 m. `10 kg` shows pounds, and `10 usd` shows the default currency.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Which other address bar habits Cmd+L must keep: paste and go, dragging the address out, the site search chip, and extensions' address bar keywords.
- Where aliases and recorded keys are listed and edited: a section of uw's settings page, the command bar alone, or both.
