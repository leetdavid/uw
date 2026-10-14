# Command bar

Status: scope agreed on 2026-10-14. Nothing is built yet. Each ticket waits for approval. It comes after the [keyboard and mouse plan](keyboard-and-mouse.md), whose commands it runs.

Implementability: **3/10** for the first stage, using the [roadmap convention](roadmap.md#implementability-estimates). Taking over Cmd+L and clicks on the address means the command bar must keep what people rely on in Chromium's address bar. Reusing Chromium's suggestion engine keeps history, bookmarks, and search from being rebuilt. The later stages are smaller: 6/10 and 7/10.

The decision to replace typing in the address bar is recorded in [ADR 0005](../adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md). The design goes on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Command bar". Nothing is drawn there yet.

## Agreed scope

### Opening it

- The [command bar](../CONTEXT.md#language) is a panel centered over the page card. The page dims a little behind it.
- Cmd+T and File → New Tab open it to open a new page. Escape creates nothing. A page picked from it opens where a blank new tab opens today, below the current tab's branch.
- Cmd+L, and a click on the address in the toolbar, open it filled with the current address, selected. A page picked from it loads in the current tab. The lock, star, and zoom icons beside the address keep their own clicks.
- Cmd+Shift+A opens it narrowed to open and recently closed tabs. Chromium's tab search popup and its toolbar button are hidden.
- Commands run on the tab that was selected when the command bar opened.
- It opens at once and never loses a key. Every typed address goes through it, so a late or dropped key breaks the [primary design rule](../product.md#principles). Keys pressed between Cmd+T and the panel appearing land in its field, in order. Added on 2026-10-14.
- A "New blank tab" command opens Chromium's new tab page.

### What it finds

- Commands, from the list the [keyboard and mouse plan](keyboard-and-mouse.md#design) defines, and Chromium's own browser commands.
- Tasks, to go to one or to move a tab there.
- Open tabs, history, bookmarks, and web search, from the same suggestion engine as Chromium's address bar. Chromium's site search shortcuts, such as typing `gh` and a query, work as they do there. An address being typed is completed inline, as the address bar does.
- Before anything is typed, the few tabs selected most recently and the commands run most recently. Cmd+L shows the current address instead.

### Layout

- The best match comes first. Fixed sections follow in a fixed order: Tabs, Tasks, Commands, Bookmarks, History, Search. Rows do not change places between sections as typing continues.
- When nothing matches well, the last rows are "Search the web for …" and "Ask uw …". Ask uw starts a [chat](../CONTEXT.md#language). The new tab page keeps its own Ask box.
- Return runs the selected row's first action. Cmd+K opens its [result menu](../CONTEXT.md#language): a tab can be switched to, closed, moved to a task, or have its link copied.

### Models

- Text typed in the command bar reaches a model only when the user picks a row that asks one: a translation or Ask uw. Nothing is sent while typing.
- Web search suggestions reach the search engine as they do from Chromium's address bar, under Chromium's own setting.

### Aliases and recorded keys

- An alias is a short word for a command, such as `mt` for Move to task. Short words for pages stay Chromium's site search shortcuts.
- The user can record a key for any command from its result menu. It works while a uw window is in front, and nowhere else.
- A recorded key must include Cmd, so it works in a terminal tab. uw refuses a key already in use and names the command that has it.

### Math, currency, and translation

- Math is worked out on the Mac and shown as the best match. Return copies the result.
- Currency uses the European Central Bank's daily reference rates, fetched at most once a day. Fetching them sends nothing the user typed. The default target is the currency of the Mac's region, and "in EUR" picks another.
- A "Translate … to <browser language>" row appears when the text looks like another language, or after typing "translate". It asks the language model only when picked. The answer shows in the command bar with a Copy action.

## Non-goals

- A plugin system. It is a possible later direction, not part of this plan.
- Hotkeys that work outside uw.
- Unit conversions.

## Design

- `browser/resources/uw_command_bar/` and `browser/ui/webui/uw_command_bar/` hold the page and its Mojo link, as the terminal's do. It shows in a top-chrome WebUI panel, the kind Chromium uses for its tab search popup.
- The page is loaded before it is needed, through Chromium's `WebUIContentsPreloadManager`, which already does this for tab search. Keys pressed before the field has focus are held by the browser and passed to the page in order once it is ready.
- Open tabs, history, bookmarks, search suggestions, and site search shortcuts come from Chromium's searchbox handler, the one behind the new tab page's search box (`chrome/browser/ui/webui/searchbox/`). The command bar adds uw's commands, tasks, fallbacks, math, currency, and translation beside them.
- Cmd+T, File → New Tab, Cmd+L, the address click, and Cmd+Shift+A are routed to the command bar where Chromium handles `IDC_NEW_TAB`, `IDC_FOCUS_LOCATION`, and `IDC_TAB_SEARCH`. Cmd+T is one of Chromium's reserved keys, so pages never receive it.
- Recent tabs and commands, aliases, and recorded keys are profile prefs.
- The math library is pinned to an exact version in `package.json`, like ghostty-web. The rates are fetched by the browser process.
- Translation is one chat request to the language model, through the connection automatic organization already uses.

## Stages

Each stage is a ticket on the [board](roadmap.md#board). The design comes before T61.

Stage (a), the core:

1. T60: spike. A preloaded top-chrome WebUI panel opens on Cmd+T without creating a tab, keeps every key typed right after Cmd+T, and lists suggestions from Chromium's searchbox handler. Proposed, not agreed: it answers the first stage's biggest unknown before the rest is built.
2. T61: the command bar on Cmd+T, with its sources, layout, and recent items.
3. T62: Cmd+L and the address click open it.
4. T63: the result menu and the fallbacks. Ask uw waits for the chat page, T47.
5. T64: Cmd+Shift+A opens it narrowed to tabs.

Stage (b): T65, aliases and recorded keys.

Stage (c): T66, math, currency, and translation.

## Acceptance checks

1. Keys sent right after Cmd+T, before the panel has drawn, all appear in its field in order, in 100 of 100 runs of a headless test. On the user's Mac the panel shows within 100 ms of Cmd+T. The 100 ms is uw's proposal.
2. Cmd+T opens the command bar over the selected tab, listing recent tabs and commands. Escape leaves the tab strip unchanged. Picking an address opens a tab below the selected tab's branch.
3. Typing "move to scratchpad" and Return moves the tab that was selected.
4. Cmd+L shows the current address selected. Editing it and Return loads the new address in the same tab. A click on the address does the same. The lock icon still opens site information.
5. For the same text, the history, bookmark, and search rows match what Chromium's suggestion engine returns.
6. The best match comes first, then the sections in their fixed order.
7. Cmd+K on a tab row offers Switch, Close, Move to task, and Copy link.
8. Text with no good match ends with "Search the web for …" and "Ask uw …".
9. Cmd+Shift+A shows open and recently closed tabs only.
10. Cmd+T, Cmd+L, and Cmd+Shift+A work with a terminal tab selected.
11. A test model server receives no request while text is typed, and one request when a translation row is picked.
12. An alias runs its command. A recorded key without Cmd is refused, a key in use is refused with its owner named, and a recorded key runs its command after a restart.
13. `2*(3+4)` shows `14` first. `100 usd in eur` uses the day's rate and shows its date.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Open questions

- Which other address bar habits Cmd+L must keep: paste and go, dragging the address out, the site search chip, and extensions' address bar keywords.
- Where aliases and recorded keys are listed and edited: a section of uw's settings page, the command bar alone, or both.
- Whether recent tabs and commands are kept per window or per profile.
- What currency conversion shows offline. Proposal: the last rates fetched, with their date.
- What the command bar keeps from a private window. Proposal: nothing after the private windows close, like a [private chat](../CONTEXT.md#language).
