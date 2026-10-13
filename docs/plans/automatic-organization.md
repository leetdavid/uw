# Automatic organization

Status: approved. The loop below is part of the [minimum usable version](../product.md#minimum-usable-version). On 2026-10-11 it moved onto [native tab groups](native-tab-tree.md) and gained a decision model setting. On 2026-10-13 it began to decide about a whole tab branch at once. See [Branch moves](#branch-moves). The first implementation is kept in commit `f113a96`.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Scores include integration, provider contracts, accuracy, latency, and maintenance, not progress.

The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), pages "Automatic organization" and "Settings".

## Agreed scope

- Settings hold two model connections. See [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md).
  - **Decision model**, optional. Providers: Jev (TypeSafe System One), the OpenAI Decisions API, or any endpoint that speaks the OpenAI Decisions API at another base URL. It answers with a choice and a confidence, never text.
  - **Language model**. Connection modes: Ollama (endpoint, model, no key) and OpenAI-compatible (base URL, model, API key).
- The decision model decides where a tab goes. With no decision model, the language model decides in one [classification call](../CONTEXT.md#language). uw asks about one tab at a time and acts on the whole [tab branch](#branch-moves).
- The language model names a task the decision model starts. Without a language model, or when the name is invalid, the task keeps the [provisional name](../CONTEXT.md#language) `New task`.
- The model that decides placement also decides when a task needs a name. The language model is asked for one only then. Agreed on 2026-10-12. It replaces a naming call on every tab change.
  - A task with the provisional name, or with no name, is named when a tab in it starts a piece of work: the same two thresholds that start a new task from the Scratchpad. A one-off page names nothing.
  - When the naming call fails or returns an unusable name, uw asks again for the next tab that changes and stays in that task, one-off or not. uw forgets which tasks are waiting when it quits. The user approved this on 2026-10-13. Before, such a task waited for the next tab that starts a piece of work.
  - A task that uw named gets a new name when it has outgrown the old one. See the [name check](#name-check).
  - The [task naming call](../CONTEXT.md#language) holds the tab that triggered it and the titles of up to 20 other tabs in the task. For a new task those are the tabs that move with it.
- uw never replaces a name the user typed. It saves the names it gave with the tabs of the task, in the session, so it still knows them after a restart. The user asked for this on 2026-10-12. A tab or window brought back with "Reopen closed tab" does not carry that data, so a task that comes back that way keeps its name.
- A task whose name the user typed shows a [name lock](../CONTEXT.md#language): a small lock at the end of its row in the sidebar. A click on it hands the name back to uw. The lock goes, the task keeps the name, and uw treats that name as one it gave, so it is replaced once the task has outgrown it. Renaming the task locks it again. A task with the provisional name shows no lock, and no task does while automatic organization is off. The user asked for a "manual" indicator and a click back to "auto" on 2026-10-13. The lock icon, its place on the row, and keeping the name on hand-back are uw's choices and open to review.
- Naming moves nothing and is not an undo step.
- Automatic organization is off until every configured connection validates. A save that fails validation turns it off.
- "Save and validate" turns automatic organization off, stores both connections, then sends one test request to each connection that is set. The language model counts as set when its model field is filled. With nothing set, it stays off.
- Each connection has "Test connection". It sends one test request for that connection as entered and saves nothing. Beside a decision model, the language model's test asks for a task name; alone, it asks for a placement. Agreed on 2026-10-12.
- A test result says the connection works and how long the answer took, or why it failed: no answer (with the network error), an HTTP error status (with the provider's own message), or an answer uw cannot use. "Details" shows the request uw sent and the start of the answer. An API key never appears there.
- "Save and validate" runs the same tests and shows each result on its connection.
- uw waits 30 seconds for an answer, in a test and while browsing, so a test that passes means the model is fast enough to use.
- A switch at the top of the settings page turns automatic organization off and on. Off sends nothing and keeps both connections. On saves and tests the connections like "Save and validate", and stays off when a test fails. Agreed on 2026-10-12.
- "Load available models" lists a connection's models with `GET <base>/models`. When the endpoint has no such list, the user types the model ID.
- A decision request contains the tab title, URL, source-tab relationship, the names of the user's named tasks, and a short readable-page-text excerpt. See [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md). For a tab in a task that uw named, a decision model's request also lists the titles of up to 20 of the tabs that make up that task, for the [name check](#name-check). A naming call for a task that already holds tabs lists them too.
- Pinned tabs are outside the tab tree. The organizer sends nothing about a pinned tab and never moves one. Pinning a tab drops the request that was out for it. A tab opened from a pinned tab is a regular tab. Agreed on 2026-10-12.
- The source-tab relationship is sent only for a tab that a link opened and that has followed links since. A blank new tab, and a tab the user took elsewhere by typing an address, searching, or picking a bookmark, has no source tab.
- A request asks two questions about placement. See [Placement rules](#placement-rules). Agreed on 2026-10-12.
  1. Which of the user's tasks does the branch belong to, or none?
  2. Is a branch outside every task the start of a task of its own, or a one-off for the Scratchpad?
- A decision model gets both as choice questions in one call, with the [name check](#name-check) as a third when it applies. The language model answers both in one JSON object. With no named task, a decision model gets only the second question.
- A task that still has the provisional name `New task` is not offered as an answer, because its name says nothing about it. A branch inside one leaves only to join a named task. It does not start a second task: the task it is in is the one it starts, and gets its name then.
- The settings page has three sliders, one for each [threshold](#thresholds): move a tab, start a task, and rename a task. Each runs from `0.50` to `0.95` in steps of `0.05` and shows its value. A change applies to the next answer. The user asked for a bar for each threshold on 2026-10-12, for now. It replaces the single "Sorting" slider of earlier that day, whose number for moving a tab had also come to decide renames.
- A tab is asked about when a link opens it, when its title changes, and when it goes to another site, 750 ms after the latest such change. A new path or new parameters under the same title on the same site ask nothing and leave an outstanding request alone: web apps keep their state in the address. The user asked for this on 2026-10-12 as "only when the title changes"; counting a change of site is uw's addition and open to review.
- A page that is still loading and has no title yet is not looked at until it has one or has finished loading, so its address is not sent as its title.
- Restored tabs and tabs dragged in from another window wait for their next change, or until [their branch is about to change](#branch-moves). A change invalidates that tab's outstanding request. Closing a branch or moving it to another window invalidates it too.
- A manual move holds the branch until that tab's next title or site change. A response that arrives after a manual move is dropped. Commands, drags, native group changes, and undo all count as manual moves.
- Tabs that sit under each other and agree move together. The top one detaches from its old parent. See [Branch moves](#branch-moves).
- Every automatic action is one undoable organization action. It shows the activity card at the foot of the sidebar with Undo, tints the moved tab for a moment, and never takes focus. After the card disappears, "Undo tab organization" in the Organize menu still works. Closed pages are never reopened.
- The activity card shows the organizer's work on one tab. While a model is asked, it names the model and the tab, with a spinner and a moving bar. After the answer it shows what the model chose, how long it took, and a bar with the number that decided it against what it had to reach. Each line is a single line that ends in an ellipsis, so the card cannot be cut off in a full sidebar. Agreed on 2026-10-12.
- The card always shows a request on its way and a move or a new task. "Show organizer activity" in the Organize menu also shows every other answer for a few seconds: left in place, suggested without enough confidence, "Naming the task" when the answer asks for a task name, failed with the HTTP status or network error, unreadable, or ignored because the tab changed. It is off by default.
- A suggestion shows "Confirm" on its card. Confirm carries the suggestion out as one undoable action, like an automatic move: it moves the branch to the suggested task or to the Scratchpad, or starts the new task and asks the language model for its name. It does nothing when the tab has changed or moved since the suggestion. Agreed on 2026-10-12.
- Starting uw with `--uw-organizer-log=<file>` appends every request the organizer makes, and its answer, to that file as one line of JSON. A placement line also says which tab it was about (`page`, an id that means nothing outside the session), where the branch was (`from`), where the answer put it (`to`), the number that decided it against what it had to reach (`confidence`, `needed`), and what the name check answered (`new_name`, zero when it was not asked). Its `result` is `name` when uw went on to ask for a task name. A tab that moved with the page above it has a line of its own, with what happened to that page. The lines hold tab titles, URLs, and page excerpts, so the browser writes nothing without the switch. The development script `pnpm run launch` passes it; `pnpm run launch:nolog` does not. Agreed on 2026-10-12.
- A confirmed suggestion adds its own line to that log with `purpose` set to `accepted`: `request` is what the model saw and `result` is the choice the user confirmed. These lines are labeled examples for tuning or training a model. uw only records them; it sends them nowhere and trains nothing itself.
- The Organize menu and the settings page provide "Pause for 1 hour", "Pause until next launch", "Pause indefinitely", and "Resume automatic organization" per [Product](../product.md#organization-controls).
- API keys are stored in the macOS Keychain, in items that every uw profile on the Mac shares. Endpoints and model IDs live in profile preferences. Keys never appear in logs, preferences, session data, or crash reports, including the organizer log.
- A run started with `--use-mock-keychain` keeps its keys in memory. It never reads or replaces a key in the Keychain. Every browser test passes that switch, and a scripted run of the app must pass it too. The user approved this on 2026-10-13, after a scripted run replaced their saved language model key.
- The settings page shows what is sent before the user enables automatic organization.
- Provider outage, malformed responses, and slow responses never block browsing. Tabs stay where they are.

## Placement rules

Agreed on 2026-10-12. `components/tab_organization/placement` holds them. The three [thresholds](#thresholds) come from the settings page.

| The model says | uw does |
| --- | --- |
| The branch belongs to another task, at the confidence threshold or more | Moves it to the end of that task. |
| It belongs to no task (confidence threshold or more) and starts a task of its own (new-task threshold or more) | Starts a new task with it. |
| It belongs to no task (confidence threshold or more), does not clearly start one, and sits in a named task | Moves it to the Scratchpad. |
| The same, and it already sits in the Scratchpad | Suggests a new task when the model leans that way. Otherwise nothing. |
| Any of the above, short of its threshold | Suggests it on the activity card. |
| It belongs to the task it is in | Nothing. |
| It belongs to no task, is not a one-off by the model's own answer, and the [name check](#name-check) reaches the rename threshold | Leaves it there and asks the language model for a new name for its task. |
| It belongs to no named task, starts a task of its own (both thresholds), and sits in a task without a name | Leaves it there and asks the language model to name that task. |
| The same, short of either threshold | Nothing. |

- Joining another task comes before the name check.
- A tab opened from another tab starts in that tab's task. It leaves on its own, like any other tab, when the page above it stays.
- The table reads one answer. [Branch moves](#branch-moves) says how the answers about the tabs of a branch combine.

### Branch moves

Agreed on 2026-10-13. `PlaceBranch` in `components/tab_organization/placement` holds the rules. The user's log showed a search for 3D-printed slippers, and a result opened from it, staying in a task about Warp. The model had put both outside every task. The old name check scored `0.75` and `0.73`, so uw renamed the Warp task instead.

- uw keeps each tab's last answer. It drops the answer when the tab's title or site changes, when the tab's task or that task's name changes, and when the user moves the tab by hand.
- uw decides about a branch as a whole, from its top page down, once no request is out for any tab in it. A request ends with an answer, a failure, or the 30-second limit.
- Before a tab in a branch moves, or its task gets a name, uw asks about every tab in that branch that it holds no answer for. It asks once per decision. This is the exception to "restored tabs wait for their next change".
- Tabs that sit under each other and whose answers agree move together as one organization action. Two answers agree when both name the same task, or both say none.
- A tab under a leaving page stays behind when its own answer says it fits the task it is in. It moves up to where the leaving page was. So does a tab the user placed by hand, and a tab uw already filed.
- A tab without an answer, or with one short of the move threshold, goes with the page above it.
- Tabs that leave together and fit no task start a task when any one of them reaches the "Start a task" threshold. Otherwise they go to the Scratchpad. The same holds in the Scratchpad: a one-off page there goes into the task that a tab under it starts.
- The top tab of the tabs that leave together decides whether their task has outgrown its name instead.
- A tab that uw filed stays where it is until it shows something else. uw does not ask about it again before then.
- While the language model is asked to name a new task, tabs opened under the leaving page join it. uw looks at the branch again when the name arrives.
- The same answers give the same moves, and carrying them out twice changes nothing. `PlacementTest.TheSameAnswersGiveTheSameMovesAndASecondPassMovesNothing` checks it.

"Reorganize all tabs" would run the same rules over every branch in the window. It is a [later stage](#later-stages).

### Thresholds

The user sets each one on the settings page, between `0.50` and `0.95`. Half is the least a probability between two answers can ask for.

| Threshold | Default | What must reach it |
| --- | --- | --- |
| Move a tab | `0.70` | The model's confidence in the task a branch belongs to, or that it belongs to none. |
| Start a task | `0.55` | The probability that a branch outside every task starts one. |
| Rename a task | `0.70` | The probability, from the [name check](#name-check), that a task has outgrown its name. |

Until 2026-10-12 the first two were fixed at `0.90` and `0.75`. For a few hours that day one slider with five stops set them together.

Measured on the 153 placement requests the organizer log recorded on 2026-10-12, replayed against the live Jev API in the two-question layout. 99 of them listed a named task. The build that recorded them moved nothing.

| Move a tab | Start a task | Joins a task | Starts a task | Fits no task, starts none | Short of the move threshold |
| --- | --- | --- | --- | --- | --- |
| `0.90` | `0.75` | 16 | 24 | 86 | 27 |
| `0.80` | `0.65` | 23 | 29 | 84 | 17 |
| `0.70` | `0.55` | 29 | 38 | 75 | 11 |
| `0.60` | `0.50` | 31 | 44 | 69 | 9 |
| `0.50` | `0.50` | 34 | 44 | 69 | 6 |

- Eleven answers that joined a task at `0.68` to `0.88` were pages of one site going to the task named after that site. A move threshold of `0.90` held all of them back.
- Four other answers between `0.62` and `0.88` followed a source line the old build sent by mistake. See [Why two questions](#why-two-questions).
- The user confirmed five new tasks that day. They scored `0.93`, `0.76`, `0.69`, `0.67`, and `0.13` on the second question. A threshold of `0.55` passes four of them, and `0.75` passes two.
- The counts say how often uw would act, not how often it would be right. Only the five confirmations are labeled.

### Name check

Agreed on 2026-10-12, with a new question since 2026-10-13. A tab that no longer matches the name of its task has either left the task, or shows that the task has outgrown its name. The decision model says which, in the same request as the placement.

- It is asked only about a tab in a task that uw named and that holds other tabs, with a decision model and a language model set. The chat format has no name check, so without a decision model a named task keeps its name.
- The question lists the titles of up to 20 tabs that make up the task and asks whether this tab is part of the same piece of work as those tabs. The answers are `same` and `different`. The question does not mention the task's name. Whether the tab fits the name is the first question's business.
- The list leaves out tabs that are being asked about at that moment, and tabs the model has put outside the task. A tab nobody asked about counts as part of the task.
- `same` at the rename threshold or more, for a tab that fits no task, keeps the tab in the task and asks the language model for a name. A one-off by the model's own answer leaves for the Scratchpad whatever the name check says. The other two thresholds have no say in a rename.
- A tab that fits its task by name renames nothing.

Until 2026-10-13 the question named the task and offered `keep` and `rename`. In real use it reached `0.70` four times, each time for an unrelated tab in a small task.

Measured on 2026-10-13. The organizer log held 18 placement requests with a name check. Those went to the live Jev API twice in each wording, with 3 made-up requests in which a task had outgrown its name.

| Name check wording | 12 real tabs the model put outside every task | 3 made-up tabs in a task that outgrew its name |
| --- | --- | --- |
| Until 2026-10-13, keep or rename | `0.19` to `0.75` | `0.59` to `0.80` |
| Same piece of work, without the tabs the model put outside the task | `0.00` to `0.20` | `0.89` to `0.99` |

- None of the 12 real tabs called for a rename. Ten had nothing to do with their task, and two started the task they went on to get.
- Three of the four that reached `0.70` in the old wording led to a rename.
- A tab opened from an unrelated search scored `0.81` to `0.89` on `same` while that search was in the list. Without it the tab scored `0.00`.
- A three-option question, keep, rename, or apart, split the vote. The made-up renames reached `0.13` to `0.69`.
- The new wording renames one of the three made-up tasks. The model kept the tabs of the other two in their task by name, so nothing needed a new name.
- One real tab that did belong scored `0.24` to `0.31` on `same`. The model kept it by name, so uw would not have read that answer.
- No real request shows a task that outgrew its name. The right-hand column is made up, so read it as a first look.
- Weak spot: a tab that sat unfiled in a task before a restart counts as part of that task, and a tab opened from it is compared with it.

### Why two questions

Measured on the 115 placement requests the organizer log recorded on 2026-10-12, replayed against the live Jev API.

- One question with `new task` and `Scratchpad` beside the tasks moved nothing. The model reached `0.90` in 6 requests, all of them tabs that stayed in their task. The two ways of being outside a task split its vote: `new task` led 68 times and never passed `0.66`.
- With two questions, the 61 requests that listed a named task reached `0.90` on the first question in 39.
- Twelve of the other 22 were tabs described as opened from a search page they had nothing to do with. Without that line all twelve were outside every task at `0.98` or more.
- Offering the tasks still named `New task` lowered `none` by `0.12` to `0.23` in 14 requests.
- Telling the model which task the tab is in made it keep unrelated tabs there, so the request does not say.
- The second question gave a new task `0.75` or more in 28 requests. The user confirmed five new tasks that day. It scored one at `0.93`, three between `0.59` and `0.77`, and one at `0.14`. So `0.75` was a cautious first value. The user's [threshold](#thresholds) replaced it.
- The same request came back up to `0.08` apart on two runs. An answer near a threshold can go either way.

## Non-goals

- A managed local model runtime and Anthropic-format providers.
- Placing a moved branch under a page in the destination task.
- Automatic merging and whole-window reorganization passes. A single branch leaving its task is in scope; splitting a task into several is not.
- User-directed proposals and "Reorganize all tabs".
- Renaming a task whose name the user typed, unless they hand it back with the name lock, and renaming without a decision model.
- Automatic tab closing and suspension.
- The 200 ms placement budget in [Product](../product.md#organization-latency). Measure first.

## Design

- `components/tab_organization/ai_protocol` builds and parses requests in three decision formats: chat completions, the OpenAI Decisions API (`POST <base>/decisions`), and TypeSafe System One (`POST <base>/systemone`). It joins the two answers into one decision and reads the name check beside them. It also builds the naming call.
- `components/tab_organization/placement` turns a decision into what uw does: nothing, a suggestion, a move, or a name for the task. `Place` is a plain function of one answer, where the branch sits, and the three thresholds. `PlaceBranch` is a plain function of the tab tree and the answers about the tabs of one branch. Chat requests allow 4096 answer tokens, because some chat models think before they answer and that thinking counts against the limit. Parsing is strict, so a malformed response can never move a tab.
- `browser/tab_organization/tab_organization_controller` keeps the name uw gave each task and writes it into the session data of every tab in the task, beside the tab's parent link.
- `browser/ui/views/tab_organization/name_lock` is the lock on a task's row. Chromium's task header creates one per task; it shows and hides itself as the controller and the on/off setting change.
- `browser/tab_organization/automatic_organizer` is owned by the window. It watches titles and URLs, sends the requests, keeps the answers, and carries out what `PlaceBranch` says through the window controller. An answer counts only while its tab is unchanged since the request.
- The organizer keeps the latest automatic change as a notice for the sidebar message.
- Excerpts reuse upstream content extraction (`components/content_extraction/content/browser/inner_text.h`).
- Browser integration owns profile prefs, Keychain secrets, `SimpleURLLoader` lifetime, debounce timers, pause state, the settings page, and the sidebar message.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. Decision formats and the naming call, with unit tests.
2. Organizer on native tab groups: requests, staleness checks, apply, undo, pause.
3. Settings page: decision model, language model, pause, and validation that gates enablement.
4. Branch moves and the same-work name check.

## Acceptance checks

1. Automatic organization turns on only after every configured connection validates. A failed save turns it off.
2. With a decision model set, a decision request goes to it in that provider's format. With none, the language model receives a classification call.
3. Opening a link in a new tab moves its branch to the end of the matched task, or starts a task. With a language model the task is named by it; otherwise it is `New task`.
4. A tab in a named task that the model places outside every task moves to the Scratchpad, or to a new task when it clearly starts one. Undo puts it back.
5. A blank new tab, and a tab the user navigated by hand, is sent with no source tab. A task named `New task` is not in the request, and an answer that names it is unreadable.
6. A new title or another site invalidates that tab's outstanding request, and an older response cannot move the tab. A new path or new parameters under the same title send nothing. `OrganizerLogBrowserTest.OnlyANewTitleOrAnotherSiteAsksAgain` checks it.
7. A manual move drops the response in flight and holds the branch until its page changes.
8. An answer short of its threshold, a refusal, or a malformed response moves nothing. `PlacementTest.*` covers every row of the [placement rules](#placement-rules) and each [threshold](#thresholds).
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
19. Each of the three sliders shows its saved threshold, saves a new one at once, and keeps it across a reload. Each threshold decides only its own kind of answer: `EachThresholdSetsHowSureOneKindOfAnswerMustBe` and `TheRenameThresholdIsItsOwn` check it in the browser, `EachThresholdHasItsOwnSlider` on the page.
20. A pinned tab is never sent to a model. Pinning a tab drops its outstanding request.
21. A task named `New task` gets a name when the model finds that a tab in it starts a piece of work and the language model returns a valid name. A one-off page sends no naming call. After a naming call that fails or returns an unusable name, the next tab that changes and stays in the task asks again, even a one-off: `AutomaticOrganizerBrowserTest.ANameThatFailedIsAskedForAgain` checks it. A tab in a task without a name does not start a second task.
22. With `--uw-organizer-log`, a placement request and a naming request each leave one line with the request, the answer, the time taken, and what uw did. A placement line says where the branch was and where it went. No API key is in the file. `OrganizerLogBrowserTest.*` checks it.

23. A tab that fits no task and would start a second one instead renames its task when the name check reaches the rename threshold. Short of it, the tab leaves as before. A tab that fits its task by name renames nothing. A name the user typed is never asked about and never replaced. An answer to a name check the request did not carry changes nothing. `OrganizerLogBrowserTest.ATaskThatOutgrowsItsNameIsRenamed` checks it.
24. A tab opened from another tab leaves its parent's task for a task of its own while its parent stays, and Undo puts it back under its parent. `AutomaticOrganizerBrowserTest.AChildTabBreaksOutOfItsParentsTask` checks it.
25. After a restart with the session restored, a task uw named is still asked about in the name check, and a task the user named is not. `AutomaticOrganizerBrowserTest.ANameUwGaveIsRememberedAfterARestart` checks it across a real restart.
26. A run with `--use-mock-keychain` does not see a key saved in the Keychain, and saving or removing a key in that run leaves the saved one as it was. `AutomaticOrganizerBrowserTest.AMockKeychainRunLeavesTheUsersKeysAlone` checks it.
27. A task with a name the user typed shows the name lock. A task uw named, a task called `New task`, and every task while automatic organization is off show none. A click on the lock hides it and keeps the name; the next placement request carries the name check, and the task can be renamed. `AutomaticOrganizerBrowserTest.OnlyANameTheUserTypedIsLocked` and `AClickOnTheLockHandsTheNameBack` check it.

28. A page that fits no task leaves with the tabs under it that fit none either, into one new task with one naming call, when any of them starts a task. A tab of the branch that nobody asked about is asked about first. One Undo puts them all back. `OrganizerLogBrowserTest.ABranchThatFitsNoTaskLeavesTogether` checks it.
29. A tab under a leaving page stays in its task, where the page was, when its own answer says it fits there. A tab under it without a sure answer goes along. `AutomaticOrganizerBrowserTest.ATabThatStillFitsItsTaskStaysBehind` checks it.
30. A tab the user placed by hand is not asked about when its branch moves, and stays. `AutomaticOrganizerBrowserTest.TabsNobodyAskedAboutAreAskedBeforeTheirBranchMoves` checks it.
31. The name check lists the tabs that make up the task, without a tab the model put outside it. `ABranchThatFitsNoTaskLeavesTogether` checks the list.

## Later stages

- The 200 ms placement path with measured provider latency.
- User-directed proposals and "Reorganize all tabs".
- Managed local runtime and Anthropic-format providers.
- A card with Undo for a new task name.
- Not asking again when a title only flips on the same page, such as an unread counter.
- A browser test for an answer that waits while another request about its branch is out. The test server answers at once, so only the unit tests cover the rule.
- Leaving tabs that sat unfiled in a task before a restart out of the name check.
- Describing a task to the model by the tabs in it, so a task named `New task` can be an answer. That sends more tab titles per request than this plan allows.

Update the roadmap with measured accuracy, confidence calibration, and latency before expanding scope.
