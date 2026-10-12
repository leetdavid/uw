# Automatic organization

Status: approved. The loop below is part of the [minimum usable version](../product.md#minimum-usable-version). On 2026-10-11 it moved onto [native tab groups](native-tab-tree.md) and gained a decision model setting. The first implementation is kept in commit `f113a96`.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Scores include integration, provider contracts, accuracy, latency, and maintenance, not progress.

The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), pages "Automatic organization" and "Settings".

## Agreed scope

- Settings hold two model connections. See [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md).
  - **Decision model**, optional. Providers: Jev (TypeSafe System One), the OpenAI Decisions API, or any endpoint that speaks the OpenAI Decisions API at another base URL. It answers with a choice and a confidence, never text.
  - **Language model**. Connection modes: Ollama (endpoint, model, no key) and OpenAI-compatible (base URL, model, API key).
- The decision model decides where a tab branch goes. With no decision model, the language model decides in one [classification call](../CONTEXT.md#language).
- The language model names a task the decision model starts. Without a language model, or when the name is invalid, the task keeps the [provisional name](../CONTEXT.md#language) `New task`.
- The model that decides placement also decides when a task needs a name. The language model is asked for one only then. Agreed on 2026-10-12. It replaces a naming call on every tab change.
  - A task with the provisional name, or with no name, is named when a tab in it starts a piece of work: the same two thresholds that start a new task from the Scratchpad. A one-off page names nothing. An unusable name leaves the task for the next tab that qualifies.
  - A task that uw named gets a new name when it has outgrown the old one. See the [name check](#name-check).
  - The [task naming call](../CONTEXT.md#language) holds the tab that triggered it and the titles of up to 20 other tabs in the task.
- uw never replaces a name the user typed. It remembers the names it gave for the session only, so a task from an earlier session keeps its name too.
- Naming moves nothing and is not an undo step.
- Automatic organization is off until every configured connection validates. A save that fails validation turns it off.
- "Save and validate" turns automatic organization off, stores both connections, then sends one test request to each connection that is set. The language model counts as set when its model field is filled. With nothing set, it stays off.
- Each connection has "Test connection". It sends one test request for that connection as entered and saves nothing. Beside a decision model, the language model's test asks for a task name; alone, it asks for a placement. Agreed on 2026-10-12.
- A test result says the connection works and how long the answer took, or why it failed: no answer (with the network error), an HTTP error status (with the provider's own message), or an answer uw cannot use. "Details" shows the request uw sent and the start of the answer. An API key never appears there.
- "Save and validate" runs the same tests and shows each result on its connection.
- uw waits 30 seconds for an answer, in a test and while browsing, so a test that passes means the model is fast enough to use.
- A switch at the top of the settings page turns automatic organization off and on. Off sends nothing and keeps both connections. On saves and tests the connections like "Save and validate", and stays off when a test fails. Agreed on 2026-10-12.
- "Load available models" lists a connection's models with `GET <base>/models`. When the endpoint has no such list, the user types the model ID.
- A decision request contains the tab title, URL, source-tab relationship, the names of the user's named tasks, and a short readable-page-text excerpt. See [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md). For a tab in a task that uw named, a decision model's request also lists the titles of up to 20 other tabs in that task, for the name check. A naming call for a task that already holds tabs lists them too.
- Pinned tabs are outside the tab tree. The organizer sends nothing about a pinned tab and never moves one. Pinning a tab drops the request that was out for it. A tab opened from a pinned tab is a regular tab. Agreed on 2026-10-12.
- The source-tab relationship is sent only for a tab that a link opened and that has followed links since. A blank new tab, and a tab the user took elsewhere by typing an address, searching, or picking a bookmark, has no source tab.
- A request asks two questions about placement. See [Placement rules](#placement-rules). Agreed on 2026-10-12.
  1. Which of the user's tasks does the branch belong to, or none?
  2. Is a branch outside every task the start of a task of its own, or a one-off for the Scratchpad?
- A decision model gets both as choice questions in one call, with the [name check](#name-check) as a third when it applies. The language model answers both in one JSON object. With no named task, a decision model gets only the second question.
- A task that still has the provisional name `New task` is not offered as an answer, because its name says nothing about it. A branch inside one leaves only to join a named task. It does not start a second task: the task it is in is the one it starts, and gets its name then.
- The settings page has a "Sorting" slider with five [eagerness](#eagerness) levels, from "Careful" to "Eager". It sets how sure a model must be before uw moves a tab without asking. A change applies to the next answer. Under the slider the page states the two thresholds of the chosen level. Agreed on 2026-10-12.
- A tab is asked about when a link opens it, when its title changes, and when it goes to another site, 750 ms after the latest such change. A new path or new parameters under the same title on the same site ask nothing and leave an outstanding request alone: web apps keep their state in the address. The user asked for this on 2026-10-12 as "only when the title changes"; counting a change of site is uw's addition and open to review.
- A page that is still loading and has no title yet is not looked at until it has one or has finished loading, so its address is not sent as its title.
- Restored tabs and tabs dragged in from another window wait for their next change. A change invalidates that tab's outstanding request. Closing a branch or moving it to another window invalidates it too.
- A manual move holds the branch until that tab's next title or site change. A response that arrives after a manual move is dropped. Commands, drags, native group changes, and undo all count as manual moves.
- The move unit is a whole tab branch. It detaches from its old parent.
- Every automatic action is one undoable organization action. It shows the activity card at the foot of the sidebar with Undo, tints the moved tab for a moment, and never takes focus. After the card disappears, "Undo tab organization" in the Organize menu still works. Closed pages are never reopened.
- The activity card shows the organizer's work on one tab. While a model is asked, it names the model and the tab, with a spinner and a moving bar. After the answer it shows what the model chose, how long it took, and a bar with the number that decided it against what it had to reach. Each line is a single line that ends in an ellipsis, so the card cannot be cut off in a full sidebar. Agreed on 2026-10-12.
- The card always shows a request on its way and a move or a new task. "Show organizer activity" in the Organize menu also shows every other answer for a few seconds: left in place, suggested without enough confidence, "Naming the task" when the answer asks for a task name, failed with the HTTP status or network error, unreadable, or ignored because the tab changed. It is off by default.
- A suggestion shows "Confirm" on its card. Confirm carries the suggestion out as one undoable action, like an automatic move: it moves the branch to the suggested task or to the Scratchpad, or starts the new task and asks the language model for its name. It does nothing when the tab has changed or moved since the suggestion. Agreed on 2026-10-12.
- Starting uw with `--uw-organizer-log=<file>` appends every request the organizer makes, and its answer, to that file as one line of JSON. A placement line also says which tab it was about (`page`, an id that means nothing outside the session), where the branch was (`from`), where the answer put it (`to`), the number that decided it against what it had to reach (`confidence`, `needed`), and what the name check answered (`new_name`, zero when it was not asked). Its `result` is `name` when uw went on to ask for a task name. The lines hold tab titles, URLs, and page excerpts, so the browser writes nothing without the switch. The development script `pnpm run launch` passes it; `pnpm run launch:nolog` does not. Agreed on 2026-10-12.
- A confirmed suggestion adds its own line to that log with `purpose` set to `accepted`: `request` is what the model saw and `result` is the choice the user confirmed. These lines are labeled examples for tuning or training a model. uw only records them; it sends them nowhere and trains nothing itself.
- The Organize menu and the settings page provide "Pause for 1 hour", "Pause until next launch", "Pause indefinitely", and "Resume automatic organization" per [Product](../product.md#organization-controls).
- API keys are stored in the macOS Keychain. Endpoints and model IDs live in profile preferences. Keys never appear in logs, preferences, session data, or crash reports, including the organizer log.
- The settings page shows what is sent before the user enables automatic organization.
- Provider outage, malformed responses, and slow responses never block browsing. Tabs stay where they are.

## Placement rules

Agreed on 2026-10-12. `components/tab_organization/placement` holds them. The two thresholds, confidence and new task, come from the [eagerness](#eagerness) setting.

| The model says | uw does |
| --- | --- |
| The branch belongs to another task, at the confidence threshold or more | Moves it to the end of that task. |
| It belongs to no task (confidence threshold or more) and starts a task of its own (new-task threshold or more) | Starts a new task with it. |
| It belongs to no task (confidence threshold or more), does not clearly start one, and sits in a named task | Moves it to the Scratchpad. |
| The same, and it already sits in the Scratchpad | Suggests a new task when the model leans that way. Otherwise nothing. |
| Any of the above, short of its threshold | Suggests it on the activity card. |
| It belongs to the task it is in | Nothing, unless the [name check](#name-check) says the task needs a new name. |
| It would start a task of its own (new-task threshold or more), or belongs to the task it is in, and the name check reaches the confidence threshold | Leaves it there and asks the language model for a new name for its task. |
| It belongs to no named task, starts a task of its own (both thresholds), and sits in a task without a name | Leaves it there and asks the language model to name that task. |
| The same, short of either threshold | Nothing. |

- A page with tabs under it does not leave its task automatically. Those tabs came from what it showed before. uw suggests the move, and Confirm moves the whole branch. Moving the page alone and promoting its children, as [Product](../product.md#automatic-tab-organization) asks, is a later stage.
- Joining another task is not held back by tabs under the page, and comes before the name check.
- A tab opened from another tab starts in that tab's task. With no tabs under it, it leaves on its own like any other tab, for a task of its own or for the Scratchpad.

### Eagerness

Agreed on 2026-10-12. The user picks a level on the settings page. Level 3 is the default. Before that day the thresholds were fixed at level 1.

| Level | Confidence | New task |
| --- | --- | --- |
| 1, Careful | `0.90` | `0.75` |
| 2 | `0.80` | `0.65` |
| 3 | `0.70` | `0.55` |
| 4 | `0.60` | `0.50` |
| 5, Eager | `0.50` | `0.50` |

- Confidence is how sure the model is of the task a branch belongs to, or that it belongs to none.
- New task is the probability that a branch outside every task starts one. `0.50` is the least it can ask for. Below that the model's own answer is the Scratchpad.

Measured on the 153 placement requests the organizer log recorded on 2026-10-12, replayed against the live Jev API in the two-question layout. 99 of them listed a named task. The build that recorded them moved nothing.

| Level | Joins a task | Starts a task | Fits no task, starts none | Short of the confidence threshold |
| --- | --- | --- | --- | --- |
| 1 | 16 | 24 | 86 | 27 |
| 2 | 23 | 29 | 84 | 17 |
| 3 | 29 | 38 | 75 | 11 |
| 4 | 31 | 44 | 69 | 9 |
| 5 | 34 | 44 | 69 | 6 |

- Eleven answers that joined a task at `0.68` to `0.88` were pages of one site going to the task named after that site. Level 1 held all of them back.
- Four other answers between `0.62` and `0.88` followed a source line the old build sent by mistake. See [Why two questions](#why-two-questions).
- The user confirmed five new tasks that day. They scored `0.93`, `0.76`, `0.69`, `0.67`, and `0.13` on the second question. The new-task threshold of level 3 passes four of them. That of level 1 passes two.
- The counts say how often uw would act, not how often it would be right. Only the five confirmations are labeled.

### Name check

Agreed on 2026-10-12. A tab that no longer matches the name of its task has either left the task, or shows that the task has outgrown its name. The decision model says which, in the same request as the placement.

- It is asked only about a tab in a task that uw named and that holds other tabs, with a decision model and a language model set. The chat format has no name check, so without a decision model a named task keeps its name.
- The question names the task and lists the titles of up to 20 of its other tabs. The answers are `keep` (the name still covers the work these tabs share) and `rename` (the tabs share work that the name is too narrow or too old to cover).
- `rename` at the confidence threshold or more keeps the tab in the task and asks the language model for a name. It counts for a tab that would otherwise start a task of its own, and for one the model keeps in its task. A one-off page leaves for the Scratchpad whatever the name check says.

Measured against the live Jev API on 2026-10-12, in two runs of the request as shipped. That day's log held two named tasks with three or more tabs, so half of the probes are made up. Read this as a first look.

| Probe | Name check |
| --- | --- |
| A real task of ten tabs that match its name | `0.17` to `0.29` |
| The same with one unrelated tab | `0.35` to `0.48` |
| Made-up tasks whose name fits | `0.13` to `0.27` |
| A made-up task with one unrelated tab | `0.49` to `0.60` |
| Made-up tasks whose name is too narrow, such as `Compare Baixa hotels` holding hotels all over Lisbon | `0.78` to `0.92` |
| A real task of three tabs, two of which already did not match its name, with one unrelated tab | `0.65` to `0.86` |

- The question does not disturb the placement. In 43 probes the placement choice never changed, and its confidence moved by `0.09` at most, which is the model's own spread.
- The placement question alone put the tabs of the too-narrow tasks outside their task at `0.60` to `0.99`. Before the name check each would have left, or started a second task.
- Weak spot: a small task that already hangs together badly. In the last row three of eight unrelated tabs would rename the task at level 3. The one-off rule stops three others: a sign-in page, a home page, and a feed.
- One made-up task whose name arguably fit answered `0.70` to `0.78`.
- A first wording, "the name describes what most of these tabs are for", followed the changed tab too closely: one unrelated tab in the ten-tab task reached `0.72`.

### Why two questions

Measured on the 115 placement requests the organizer log recorded on 2026-10-12, replayed against the live Jev API.

- One question with `new task` and `Scratchpad` beside the tasks moved nothing. The model reached `0.90` in 6 requests, all of them tabs that stayed in their task. The two ways of being outside a task split its vote: `new task` led 68 times and never passed `0.66`.
- With two questions, the 61 requests that listed a named task reached `0.90` on the first question in 39.
- Twelve of the other 22 were tabs described as opened from a search page they had nothing to do with. Without that line all twelve were outside every task at `0.98` or more.
- Offering the tasks still named `New task` lowered `none` by `0.12` to `0.23` in 14 requests.
- Telling the model which task the tab is in made it keep unrelated tabs there, so the request does not say.
- The second question gave a new task `0.75` or more in 28 requests. The user confirmed five new tasks that day. It scored one at `0.93`, three between `0.59` and `0.77`, and one at `0.14`. So `0.75` was a cautious first value. The [eagerness](#eagerness) setting replaced it.
- The same request came back up to `0.08` apart on two runs. An answer near a threshold can go either way.

## Non-goals

- A managed local model runtime and Anthropic-format providers.
- Placing a moved branch under a page in the destination task.
- Automatic merging and whole-window reorganization passes. A single branch leaving its task is in scope; splitting a task into several is not.
- User-directed proposals and "Reorganize all tabs".
- Renaming a task whose name the user typed, and renaming without a decision model.
- Keeping track of which names uw gave across a restart.
- Automatic tab closing and suspension.
- The 200 ms placement budget in [Product](../product.md#organization-latency). Measure first.

## Design

- `components/tab_organization/ai_protocol` builds and parses requests in three decision formats: chat completions, the OpenAI Decisions API (`POST <base>/decisions`), and TypeSafe System One (`POST <base>/systemone`). It joins the two answers into one decision and reads the name check beside them. It also builds the naming call.
- `components/tab_organization/placement` turns a decision into what uw does: nothing, a suggestion, a move, or a name for the task. It is a plain function of the answer, where the branch sits, and the two thresholds of the eagerness level. Chat requests allow 4096 answer tokens, because some chat models think before they answer and that thinking counts against the limit. Parsing is strict, so a malformed response can never move a tab.
- `browser/tab_organization/automatic_organizer` is owned by the window. It watches titles and URLs, sends the requests, and applies a decision through the window controller only if the branch is unchanged since the request.
- The organizer keeps the latest automatic change as a notice for the sidebar message.
- Excerpts reuse upstream content extraction (`components/content_extraction/content/browser/inner_text.h`).
- Browser integration owns profile prefs, Keychain secrets, `SimpleURLLoader` lifetime, debounce timers, pause state, the settings page, and the sidebar message.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. Decision formats and the naming call, with unit tests.
2. Organizer on native tab groups: requests, staleness checks, apply, undo, pause.
3. Settings page: decision model, language model, pause, and validation that gates enablement.

## Acceptance checks

1. Automatic organization turns on only after every configured connection validates. A failed save turns it off.
2. With a decision model set, a decision request goes to it in that provider's format. With none, the language model receives a classification call.
3. Opening a link in a new tab moves its branch to the end of the matched task, or starts a task. With a language model the task is named by it; otherwise it is `New task`.
4. A tab in a named task that the model places outside every task moves to the Scratchpad, or to a new task when it clearly starts one. Undo puts it back. A page with tabs under it is only suggested, and Confirm moves its branch.
5. A blank new tab, and a tab the user navigated by hand, is sent with no source tab. A task named `New task` is not in the request, and an answer that names it is unreadable.
6. A new title or another site invalidates that tab's outstanding request, and an older response cannot move the tab. A new path or new parameters under the same title send nothing. `OrganizerLogBrowserTest.OnlyANewTitleOrAnotherSiteAsksAgain` checks it.
7. A manual move drops the response in flight and holds the branch until its page changes.
8. An answer short of its threshold, a refusal, or a malformed response moves nothing. `PlacementTest.*` covers every row of the [placement rules](#placement-rules) and every [eagerness](#eagerness) level.
9. Closing a branch or moving it to another window turns its pending response into a no-op.
10. One undo reverses task creation, the move, and the name together. It still works from the Organize menu after the message disappears.
11. Pause stops requests and drops responses in flight across all windows. Resume restores them. Manual commands keep working while paused.
12. Each request contains the documented scope only. No API key appears in logs, preferences, session data, or crash reports. A request body is written only to the organizer log, and only when uw was started with `--uw-organizer-log`.
13. `pnpm run check` passes. `unit_tests` passes `PageTreeTest.*`, `AiProtocolTest.*`, and `PlacementTest.*`. `browser_tests` passes `TabOrganizationControllerBrowserTest.*`, `AutomaticOrganizerBrowserTest.*`, `OrganizerLogBrowserTest.*`, and `UwOrganizationPageBrowserTest.*`.
14. "Test connection" tests one connection, saves nothing, and names the reason for a failure. `UwOrganizationPageBrowserTest.*` covers a pass, an unusable answer, an HTTP error with the provider's message, a refused redirect, and a connection that is not filled in.
15. The switch turns automatic organization off without a request and on only after the connections pass their tests again.
16. Each request shows the activity card with the model and the tab. With "Show organizer activity" on, a failed request, an unreadable answer, a low-confidence suggestion, and a choice to leave the tab each show their own headline, with the confidence where the model gave one.
17. With 30 more tabs than fit, the card keeps its full height, lies inside the sidebar foot, and overlaps nothing. `AutomaticOrganizerBrowserTest.TheCardFitsInACrowdedSidebar` checks it.
18. Confirm on a suggestion moves the branch or starts the task, offers Undo, and sends no second placement request. A suggestion for a tab that has since changed is not carried out. With the log on, the confirmation is one `accepted` line.
19. The "Sorting" slider shows the saved level and its two thresholds, saves a new level at once, and keeps it across a reload. The same answer that is only suggested at level 1 moves the tab at level 3.
20. A pinned tab is never sent to a model. Pinning a tab drops its outstanding request.
21. A task named `New task` gets a name when the model finds that a tab in it starts a piece of work and the language model returns a valid name. A one-off page sends no naming call. An unusable name leaves the task for the next such tab. A tab in a task without a name does not start a second task.
22. With `--uw-organizer-log`, a placement request and a naming request each leave one line with the request, the answer, the time taken, and what uw did. A placement line says where the branch was and where it went. No API key is in the file. `OrganizerLogBrowserTest.*` checks it.

23. A tab that would start a second task instead renames its task when the name check reaches the confidence threshold. Short of it, the tab leaves as before. A name the user typed is never asked about and never replaced. An answer to a name check the request did not carry changes nothing. `OrganizerLogBrowserTest.ATaskThatOutgrowsItsNameIsRenamed` checks it.
24. A tab opened from another tab, with no tabs under it, leaves its parent's task for a task of its own, and Undo puts it back under its parent. `AutomaticOrganizerBrowserTest.AChildTabBreaksOutOfItsParentsTask` checks it.

## Later stages

- The 200 ms placement path with measured provider latency.
- User-directed proposals and "Reorganize all tabs".
- Managed local runtime and Anthropic-format providers.
- A card with Undo for a new task name.
- Not asking again when a title only flips on the same page, such as an unread counter.
- Moving a repurposed parent page alone and promoting the tabs under it.
- Describing a task to the model by the tabs in it, so a task named `New task` can be an answer. That sends more tab titles per request than this plan allows.

Update the roadmap with measured accuracy, confidence calibration, and latency before expanding scope.
