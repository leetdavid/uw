# Command bar

Status: scope agreed on 2026-10-14. Widened on 2026-10-15: one list ranked by use, instant results only, sending the typed text to a chat or a terminal, and sums with units and currencies. T60, the spike, started on 2026-10-15 at the user's request. The other tickets wait for approval. The command bar runs the commands of the [keyboard and mouse plan](keyboard-and-mouse.md).

Implementability, using the [roadmap convention](roadmap.md#implementability-estimates): **6/10** for the command and task rows (T61), and **4/10** for ranking by picks (T111). Taking over Cmd+L and clicks on the address means the command bar must keep what people rely on in Chromium's address bar. One ranked list has to put uw's commands and tasks on the same scale as Chromium's history and bookmark scores. Reusing Chromium's suggestion engine keeps history, bookmarks, and search from being rebuilt. The later tickets: sending to a chat 7/10, running in a terminal 6/10, aliases and keys 6/10, and the calculator 6/10.

The decision to replace typing in the address bar is recorded in [ADR 0005](../adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md). The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Command bar": the frames "Command bar: as you type", "Command bar: before typing", "Command bar: every state", "Calculator: units and currencies", "Calculator: every state", "Time: zones and dates", "Time: every state", "Chat: send the text with Cmd+Return", "Terminal: run the text with Option+Return", and "Chat and terminal: every state". Redrawn on 2026-10-15 for the wider scope. The two calculator frames were redrawn on 2026-10-19 for the rules agreed that day. The two time frames were drawn on 2026-10-21 for T143, and the calculator's unread-word state was redrawn that day. The look's rules are in the canvas style guide `uw-command-bar`. The user has not signed off yet.

## Agreed scope

### Opening it

- The [command bar](../CONTEXT.md#language) is a panel centered over the page card. The page dims a little behind it.
- Cmd+T and File → New Tab open it to open a new page. Escape on an empty field closes it and creates nothing. A page picked from it opens where a blank new tab opens today, below the current tab's branch.
- "New child tab" and Ctrl+Cmd+T open it too. A page picked from it opens as the first child of that tab, per [New child tab](keyboard-and-mouse.md#new-child-tab). Agreed on 2026-10-15.
- The "+" on a task's row opens it for that task, per [Mouse actions](keyboard-and-mouse.md#mouse-actions). A picked page, a Cmd+Return chat, and an Option+Return terminal each open at the end of the task, selected. A row for an open tab still switches to it. Agreed on 2026-10-21.
- A "Command bar" section on uw's settings page has a switch, "Cmd+T opens the command bar". It is on by default and takes effect at once in every window. Off, Cmd+T and File → New Tab open a blank tab. Until T98 the command bar cannot open pages, and the section says so. Agreed on 2026-10-15.
- Cmd+L, and a click on the address in the toolbar, open it filled with the current address, selected. A page picked from it loads in the current tab. The lock, star, and zoom icons beside the address keep their own clicks.
- Cmd+Shift+A opens it narrowed to open and recently closed tabs. Chromium's tab search popup and its toolbar button are hidden.
- Cmd+Shift+P opens it with `>` in the field, ready for a shell command. See [Sending the text to a chat or a terminal](#sending-the-text-to-a-chat-or-a-terminal). Agreed on 2026-10-15.
- Commands run on the tab that was selected when the command bar opened.
- It opens at once and never loses a key. Every typed address goes through it, so a late or dropped key breaks the [primary design rule](../product.md#principles). Keys pressed between Cmd+T and the panel appearing land in its field, in order. Added on 2026-10-14.
- A "New blank tab" command opens Chromium's new tab page.
- A click elsewhere, or switching apps, closes it and keeps what it showed: the text, the caret, the rows, the selected row, and the calculator's card. Every way of opening it shows them again, except Cmd+Shift+P, which starts an empty `>`. Each window keeps its own, with no time limit, until it is used or Escaped. Opening a row, Cmd+Return, Option+Return, and copying an answer forget it. Agreed on 2026-10-26 at the user's request.
- Escape with text in the field empties the command bar, its `>` or site search chip too. Escape on an empty field closes it. Agreed on 2026-10-26: before, Escape always closed it.

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
- ↑ and ↓ put the selected row's text in the field, as Chromium's address bar does: a page's address, or a search's words. The caret goes to the end, so the text can be edited before Return. Going back to the first row brings back what was typed. Agreed on 2026-10-22 at the user's request.
- Text uw puts in the field, from a row or an address completion, is muted after what was typed. With `pu` typed and the row for `pulse.bilby.ai` selected, `pu` is in full ink and `lse.bilby.ai` is muted. Once edited, all of the text is typed text. Agreed on 2026-10-22.

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
- The completion is drawn after the caret but is not in the field, so the caret shows where typing goes. Typing its next letters keeps the rest. Tab, →, and End turn it into typed text. Backspace deletes the last typed letter and the completion goes with it, and so does moving the caret. Changed on 2026-10-22 at the user's request: before, the completion was selected text, as in Chromium, so no caret showed and Backspace removed only the completion.

### Models

- Text typed in the command bar reaches a model only when the user asks for one: a translation row, Ask uw, or Cmd+Return. Nothing is sent while typing.

### Aliases and recorded keys

- An alias is a short word for a command, such as `mt` for Move to task. Short words for pages stay Chromium's site search shortcuts.
- The user can record a key for any command from its result menu. It works while a uw window is in front, and nowhere else.
- A recorded key goes into uw's key list and follows its rules: see [Keyboard shortcuts](keyboard-and-mouse.md#keyboard-shortcuts). Changed on 2026-10-19: a key already in use now moves to the new command instead of being refused.

### Math, units, currency, and translation

Widened on 2026-10-15. Before, unit conversions were a non-goal.

- Math, units, and currencies are worked out on the Mac and shown as the first row. Return copies the result.
- The answer shows when the text has a number, is not only a number, and mathjs reads all of it apart from an unfinished end: `5 * pi` answers, and `pi`, `cup`, and `2024` show nothing. Text with a word mathjs does not read gets no card, only search results, since it may be a title: `1/2 cup flour`, `100 usd to xyz`, `covid-19`, and `iphone 15 pro` stay searches. A no-answer card shows only when every word was read but the sum does not work out: `10 kg + 5 m` says it cannot add a mass to a length. Agreed on 2026-10-17. Changed on 2026-10-21: before, a word that was not a unit showed a card saying so.
- Units and currencies mix in one sum: `10 HKD + 10 EUR to USD`, or `20 kg * 10 m/s/s / 5W`, which shows 40 s/m.
- The answer is in mathjs's units, which combine base units into named ones, so kg·m/s² becomes N. Typed units that multiply into a unit with its own name stay in it: `100 W * 3h` shows 300 Wh, and `2 A * 3 h` shows 6 Ah. `to N/W` converts to another unit. Changed on 2026-10-17: before, the typed units were kept.
- A power or a current times any time answers in watt-hours or amp-hours, with the prefix typed: `2 kW * 30 min` shows 1 kWh, and `500 mA * 90 min` shows 750 mAh. Agreed on 2026-10-19.
- `min` is minutes, unless `(` follows it.
- Spelled-out unit names and currency codes ignore case: `milliAmp`, `Kilometers`, and `usd` work. Unit symbols keep it, so `mA` and `MA` stay different. Agreed on 2026-10-19.
- A cup is always the metric cup of 250 ml, a teaspoon (`tsp`) 5 ml, and a tablespoon (`tbsp`) 15 ml, so `1/2 cup to tsp` shows 25 tsp. Agreed on 2026-10-17.
- Units run together read as their product, as `Wh` does: `Wm` is W·m, and `Nm` is N·m. A sum reads left to right, so `200 N / 5 Wm/s` shows 40 m⁻², and `200 N / (5 Wm/s)` shows 40 s²/m². Agreed on 2026-10-17.
- A half-typed sum shows the answer for the part that is complete: `10m *` shows 10 m.
- The answer is one card, with no rows of the same value in other units under it. `to ft` converts. Agreed on 2026-10-19.
- Plain math shows up to 10 significant digits, an answer with units up to 3, and a currency up to 2 decimals: `2/3` shows 0.6666666667, `5 km` shows 3.11 mi, and `100 usd` shows HK$777.40. Trailing zeros are dropped, except a currency's cents. A whole number shows every digit: `1 week to minutes` shows 10,080 min. A unit spelled as a word takes its plural: `66 days to weeks` shows 9.43 weeks. Return copies the answer as shown. Agreed on 2026-10-17; whole numbers and plurals agreed on 2026-10-21.
- The answer shows beside the sum as mathjs read it, so the user can check it against what they meant. It is not tidied, only spelled with × and ÷: `10 m/s/s` shows 10 m ÷ s ÷ s. Under it, the units it read are named in words, such as "meters, seconds". Only an unfinished end is set aside; any other name that is not a unit or currency hides the card. Agreed on 2026-10-17.
- A bare amount converts: a unit to the other measurement system, so `10 kg` shows pounds, and a currency to the default currency.
- Currency uses the European Central Bank's daily reference rates, fetched at most once a day. Fetching them sends nothing the user typed. Offline, the last rates fetched are used and their date is shown.
- Amounts can be typed with codes or symbols. `$` is always the US dollar; other dollars take their prefix, as in `HK$`, `A$`, `C$`, `NZ$`, and `S$`. `€` is the euro, `£` the pound, `₩` the won, `₹` the rupee, `¥` the yen, and `CN¥` the yuan. Agreed on 2026-10-19.
- Amounts can be typed in shorthand: `k` is a thousand, `m` a million, `b` or `bn` a billion, and `t` or `tn` a trillion, in either case and touching the number. The words `thousand`, `million`, `billion`, and `trillion` work too. Every suffix works on an amount with a currency code or symbol: `180k usd to hkd`, `$5K`, and `€1.5m`. Other numbers take only the suffixes that are not units, so `5k + 3k` shows 8,000, while `5m` stays 5 meters and `2B` 2 bytes. The sum shows the amount in full, as in 180,000 USD. Agreed on 2026-10-26.
- The default currency is the currency of the Mac's region. A "Default currency" setting in the Command bar section of uw's settings page picks another, from the currencies the last rates cover; "Mac's region" comes first and follows the region if it changes. `in EUR` picks one for a single sum.
- A "Translate … to <browser language>" row appears when the text looks like another language, or after typing "translate". It asks the language model only when picked. The answer shows in the command bar with a Copy action. Split into T112 on 2026-10-17, after T66.

### Time and time zones

Agreed on 2026-10-21 at the user's request, as Raycast does. The calculator also works out times, time zones, dates, the gaps between them, and Unix timestamps. Everything above still applies: the same card, ink levels, digits, rule for unread words, and Return to copy.

These examples are typed in Hong Kong at 6:24 PM on Tuesday, Oct 20, 2026:

| Typed | Pill under the sum | Answer | Pill under the answer |
| --- | --- | --- | --- |
| `now +` | Hong Kong | 6:24 PM | Tuesday, Oct 20 |
| `today` | Hong Kong | Tuesday, Oct 20, 2026 | Day 293 of 2026 |
| `3pm` | Hong Kong | 3:00 PM | Today, 3 h 24 min ago |
| `dec 25` | | Friday, Dec 25, 2026 | In 66 days |
| `dec 25 - today` | | 66 days | 2 months 5 days |
| `5:30pm - 9:15am` | | 8 h 15 min | |
| `today + 3 weeks` | | Tuesday, Nov 10, 2026 | In 3 weeks |
| `now + 100 minutes to UTC` | Today, 6:24 PM | 12:04 PM | Today, UTC |
| `3pm London` | London time | 10:00 PM | Today, Hong Kong |
| `1760000000 to date` | Unix time | Oct 9, 2025, 4:53:20 PM | Thursday |

- Day words are `now`, `today`, `tomorrow`, and `yesterday`.
- A date has a month name, as in `dec 25`, `25 dec`, and `december 25 2026`, or is written year first, as in `2026-12-25`. Slashes stay math: `12/25` is 0.48, and `3/4` is 0.75. A date with no year is the next one.
- A weekday counts with a time, a place, or a sum: `friday 3pm London`, or `friday - today`, which shows 3 days. A weekday is the next one, or today on that day. `next friday` is the Friday of next week, Oct 30, and `last friday` the most recent, Oct 16.
- `in` before a length of time adds it: `today in 3 days` and `now in 2 hours`. Before a place or a unit, `in` converts. A bare `in 3 days` or `3 days ago` is not read.
- Places are official zone names and their cities, UTC and GMT with an offset, countries with one zone, and about 50 big cities that share another city's zone, such as San Francisco, Seattle, Boston, Beijing, and Mumbai.
- Each zone abbreviation stands for one place and follows its clock. The most common place wins: IST is India, CST is US Central, and BST is London. PST and PDT both mean Los Angeles time, so `3pm PST` on Oct 20 shows 6:00 AM tomorrow. The pill under the sum names the place read, as in "IST · India".
- `1760000000 to date` reads seconds, and a 13-digit number reads as milliseconds, with the pill "Unix time · ms". `now to unix` shows 1792491840, and `dec 25 to unix` shows 1798128000, midnight in the Mac's time zone. Timestamps show in seconds, without commas.
- A gap keeps its sign and is worked out as written: `9:15am - 5:30pm` shows −8 h 15 min, and `2am - 10pm` shows −20 h, while `tomorrow 2am - 10pm` shows 4 h. A gap between dates answers in days, with months in its pill. A gap converts: `dec 25 - today to hours` shows 1,584 h, and `to weeks` shows 9.43 weeks.
- `now +`, `today`, `3pm`, `dec 25`, and `next friday` show a card on their own. A bare `now`, a bare `friday`, `tokyo`, `tokyo hotels`, `dinner tomorrow`, `meeting at 3pm`, and a bare `1760000000` show only search results, and so do `now in Narnia` and `today + 3 bananas`. A card says why only when everything was read but cannot work out: `feb 30` says "Feb 30 is not a date", and `13:75` says "13:75 is not a time".
- Times follow the Mac's 24-hour setting, so `3pm` shows 15:00 with it on.
- A card built on `now` updates when the minute changes, so Return copies the current time.
- Month ends and daylight saving follow Temporal: `jan 31 + 1 month` is Feb 28, and `2:30am New York` on Mar 14, 2027, a time daylight saving skips, is 3:30 AM.
- Numbers and dates are darkest, words such as `now` and zone names lighter, and `+`, `−`, `to`, and `in` lightest.

## Non-goals

- A plugin system. It is a possible later direction, not part of this plan.
- Hotkeys that work outside uw.
- Live suggestions from the search engine. Added on 2026-10-15.

## Design

- `browser/resources/uw_command_bar/` and `browser/ui/webui/uw_command_bar/` hold the page and its Mojo link, as the terminal's do. It shows in a top-chrome WebUI panel, the kind Chromium uses for its tab search popup.
- While the switch is on, each window keeps its command bar page loaded and shows it in a `WebUIBubbleDialogView` over the page card, from `browser/ui/views/command_bar/`. Chromium's `WebUIContentsPreloadManager` keeps one page for all of Chromium and picks which, and tab search's bubble drops its page 30 seconds after closing, so neither keeps it ready. Keys pressed before the page can take them are held by the browser and passed to the page in order once the page says its field has focus. Until the page's first frame is on screen, the browser refuses its keys, so a refused key stays held and is tried again a frame later. After one second the browser stops holding.
- Open tabs, history, bookmarks, past searches, and site search shortcuts come from Chromium's suggestion engine, asked for instant results only with `AutocompleteInput::set_omit_asynchronous_matches`, the mode Chromium uses for pasted text. The spike used the new tab page's searchbox handler (`chrome/browser/ui/webui/searchbox/`), which loads a picked page inside the panel. T98 subclasses it as `UwCommandBarHandler`: it opens a picked row in a new tab, runs an open-tab row's switch action, names each row's kind for the page, and asks for instant results only through a hook `uw-command-bar.patch` adds to Chromium's `SearchboxHandler`. The panel's `OpenURLFromTab` hands the page to the window, which opens it where `chrome::NewTab` would put a blank tab. The command bar adds uw's commands, tasks, fallbacks, math, currency, and translation beside them.
- A text field cannot color part of its text, so the field's own text is invisible and a layer under it draws the typed text and the muted rest, scrolled with the field. The completion is drawn only in that layer. The arrow keys fill the field through the fill text Chromium's searchbox uses, `KeywordModeManager.formatMatchFillIntoEdit`.
- Chromium's rows keep its scores. uw scores its own rows on the same scale and adds what it learned from picks to every row.
- Cmd+T, File → New Tab, Cmd+L, the address click, and Cmd+Shift+A are routed to the command bar where Chromium handles `IDC_NEW_TAB`, `IDC_FOCUS_LOCATION`, and `IDC_TAB_SEARCH`. Cmd+T is one of Chromium's reserved keys, so pages never receive it. Cmd+Shift+P is not used by Chromium on the Mac.
- What uw learns from picks, aliases, and the default currency are profile prefs. Recorded keys are the same in every profile. A history deletion clears what uw learned.
- Cmd+Return and Option+Return reach the browser over the searchbox connection the page already has: the page opens with the Cmd or Option flag set, and `UwCommandBarHandler` hands the query's text to its window. Cmd+Shift+P has the handler set the field to `>`, through the searchbox page's `SetInputText`.
- The page handles Escape, so the browser holds it with the other keys typed right after Cmd+T, and the page asks the browser to close. When the panel closes for any reason but losing focus, the browser has the page forget its text through the same `SetInputText`.
- Cmd+Return opens `uw://chat` the way New chat does, and the chat page sends the text as if typed. The chat's agent adopts the selected tab as tab 1, as it adopts its own tabs after a restart.
- Option+Return writes the text and a Return to the shell through the one function every byte to the shell passes, per the [terminal tab design](terminal-tab.md#design).
- The math library is pinned to an exact version in `package.json`, like ghostty-web. mathjs 15.2.0, agreed on 2026-10-17. It has units and lets uw add each currency as a unit. The rates are fetched by the browser process: `ExchangeRates` downloads the bank's daily file, reads it with Chromium's data decoder, and keeps it in profile prefs. The page asks for them over `uw_command_bar.mojom`, which also copies an answer and closes the command bar.
- Time math uses Temporal, which uw's Chromium ships, and no other library. uw reads the times, dates, places, and timestamps itself; the city list and the abbreviation table sit beside `calculator.ts`. The browser passes the Mac's time zone and 24-hour setting to the page, as it passes the default currency, because the page's `Intl` does not see the Mac's 24-hour switch. Tests run Node with `--harmony-temporal`, in one process, because Node's test runner does not pass that flag to the files it starts.
- Translation is one chat request to the language model, through the connection automatic organization already uses.

## Stages

Each stage is a ticket on the [board](roadmap.md#board), built in this order. Changed on 2026-10-16: T99, T82, and T81 moved ahead of T61 at the user's request, and T82 no longer waits for T54. Changed again the same day: the user put commands off, so T66 comes before T61. Changed on 2026-10-21: T143, time math, comes after T66 and before translation. Changed on 2026-10-26: T213 comes after T156.

1. T60: spike. A preloaded top-chrome WebUI panel opens on Cmd+T without creating a tab, keeps every key typed right after Cmd+T, and lists suggestions from Chromium's searchbox handler.
2. T98: the command bar opens what is picked, in the design's look, from Chromium's instant results. Added on 2026-10-16 so Cmd+T opens pages before T61.
3. T99: addresses complete inline, Tab takes the completion, and Return opens the completed address. Added on 2026-10-16.
4. T82: Option+Return, `>`, and Cmd+Shift+P run the text in a new terminal.
5. T81: Cmd+Return sends the text to a new chat, with the selected tab handed to the agent.
6. T66: math, units, currency, and the default currency setting.
7. T143: time and time zones. Added on 2026-10-21.
8. T156: the arrow keys put a row's text in the field, and filled text is muted. Added on 2026-10-22.
9. T213: the command bar keeps its text when it loses focus, and Escape empties the field first. Added on 2026-10-26.
10. T112: translation. Split from T66 on 2026-10-17.
11. T61: command and task rows, with their keys and tab counts, and the task of each tab row. Split on 2026-10-16.
12. T111: one list ranked by picks, and the most-picked items before typing. Split from T61 on 2026-10-16.
13. T62: Cmd+L and the address click open it.
14. T63: the result menu, Reset ranking, the Cmd+K choices for chats and terminals, and the fallbacks.
15. T64: Cmd+Shift+A opens it narrowed to tabs.
16. T65: aliases, and recording a key from the result menu into T122's key list.

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
14. An alias runs its command. A key recorded from the result menu shows in the Keyboard shortcuts section and runs its command after a restart.
15. `2*(3+4)` shows `14` first. `100 usd in eur` uses the day's rate and shows its date. `10 HKD + 10 EUR to USD` adds both at the day's rates. `20 kg * 10 m/s/s / 5W` shows 40 s/m, `100 W * 3h` shows 300 Wh, `90 min to h` shows 1.5 h, `1/2 cup to tsp` shows 25 tsp, and `10m *` shows 10 m. `10 kg` shows pounds, and `10 usd` shows the default currency.
16. With the clock at 6:24 PM on Tuesday, Oct 20, 2026, in Hong Kong: `now + 100 minutes to UTC` shows 12:04 PM, `3pm London` shows 10:00 PM, `dec 25 - today` shows 66 days, `next friday` shows Friday, Oct 30, 2026, `1760000000 to date` shows Oct 9, 2025, 4:53:20 PM with Thursday, and `now to unix` shows 1792491840. `2026-12-25` shows a date, and `3/4` shows 0.75. `now`, `friday`, and `now in Narnia` show no card, and `feb 30` says it is not a date. With the Mac's 24-hour setting on, `3pm` shows 15:00. A card built on `now` shows the next minute without retyping.
17. With a page visited by its address, typing the start of the address shows the rest muted after the caret. Backspace deletes a typed letter and the rest goes; → takes it. ↓ to another page's row puts its address in the field with the caret at the end, what was typed in full ink and the rest muted, and a long address shows its end. Backspace edits it, and all of it turns to full ink. ↑ back to the first row shows what was typed and its completion.
18. With `git` typed and the caret after `g`, a click on the page closes the command bar, and Cmd+T shows `git` with the caret after `g` and the same rows; Ctrl+Cmd+T does too. Escape empties the field and leaves the panel open, and a second Escape closes it, so the next Cmd+T opens empty. After a pick, Cmd+Return, Option+Return, or a copied answer, the next Cmd+T opens empty. Cmd+Shift+P opens an empty `>` over kept text. Typing `a` and Escape right after Cmd+T, before the panel has drawn, leaves it open and empty.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Which other address bar habits Cmd+L must keep: paste and go, dragging the address out, the site search chip, and extensions' address bar keywords.
- Where aliases are listed and edited: a section of uw's settings page, the command bar alone, or both. Keys are listed and edited in the [Keyboard shortcuts](keyboard-and-mouse.md#keyboard-shortcuts) section, agreed on 2026-10-19.
