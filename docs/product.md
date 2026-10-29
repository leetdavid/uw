# Product

The [main features](#main-features) are agreed. Detailed feature behavior below is proposed unless marked agreed.

## Goal

Make everyday browsing easier to manage, automate, and customize.

## Main features

Agreed on 2026-10-15: uw is built around three features. Each has its own section below.

| Feature | What it does | When |
| --- | --- | --- |
| [Automatic tab organization](#automatic-tab-organization) | Files tabs into tasks as the user browses. | Minimum usable version |
| [Agent automation](#agent-automation) | An agent browses and carries out requests for the user. | v2 |
| [Integrations](#integrations) | Brings tools from the user's computer into uw, starting with a terminal tab. | Later addition |

The [board](plans/roadmap.md#board) shows what is built for each.

## Audience

Agreed: build for the project owner's daily browsing first. A broader target audience remains undecided.

Agreed on 2026-10-26: anyone may download uw as an early preview. It is marked as rough, may break, and comes with no support promise. The [distribution plan](plans/distribution.md) has the details.

## Minimum usable version

Agreed: the first version targets macOS desktop. Chrome-like everyday browsing with vertical tabs is enough to start daily use. Other planned features can follow.

Agreed: keep uw branding minimal and use native vertical tabs. Changed on 2026-10-22, when the user asked to "disable horizontal tabs": tabs are always vertical. No menu or setting switches to horizontal tabs, and a profile that chose them before opens with vertical tabs.

Agreed on 2026-10-14, for now: the logo is the two-ink double u in [Logo](logo.md). It is the app icon on macOS. Logos inside the browser are still Chromium's.

Agreed on 2026-10-11: the look follows Arc browser. The window is tinted, the page sits in a rounded card, pinned tabs are tiles, and a task reads as a folder. The tint is a two-color gradient that the user can change on uw's settings page, with a light and a dark default. Text on the tint turns white or near-black, whichever reads better, and a color that would leave it hard to read is made lighter or darker. Underneath, tabs and tasks stay Chromium's native vertical tabs and tab groups. The [plan](plans/arc-style-look.md) has the details. Changed on 2026-10-16: the tint becomes a [theme](plans/themes.md), as in Arc: up to three colors with intensity and grain, picked over the window, one per profile. Dark mode is black by default, with a switch to show the theme instead. Changed on 2026-10-21: one theme per [workspace](#workspaces).

Agreed: manual setup is acceptable for the first usable version. Automatic browser-data import is not required for that version.

Agreed: performance and stability are basic usability requirements. Crashes, disruptive slowdowns, and broken daily workflows block acceptance. Later features must preserve that baseline.

Agreed: the [automatic organization MVP](plans/automatic-organization.md) is part of the minimum usable version. It automatically files tab branches into task groups. Automatic tab closing and suspension remain deferred.

### Required extensions

Agreed: the first usable version must support installing and using:

- 1Password
- uBlock Origin Lite
- Improve YouTube
- SponsorBlock
- Return YouTube Dislike
- A picture-in-picture extension, exact listing to confirm
- Keepa
- SteamDB

Other day-one browser workflow requirements remain open.

## Principles

- Agreed highest priority: the browser must not get in the user's way. Browsing and deliberate user actions take priority over automation. AI work must not interrupt or stall navigation, typing, tab switching, or tab manipulation.
- Keep the user in control. Automatic changes should be understandable and reversible.
- Measure performance against a [repeatable baseline](architecture.md#performance).

## Automatic tab organization

Agreed priority: the [automatic organization MVP](plans/automatic-organization.md) is part of the [minimum usable version](#minimum-usable-version). It classifies continuously into existing tasks and creates named new tasks. Jev fast classification, the separate naming call, user-directed proposals, and [Tidy](plans/scratchpad-buttons.md) land later.

Agreed scope: the first tab-management release focuses on organization. Infer the user's browsing task, then decide how to organize the tabs. Preserve familiar Chromium behaviors where possible and expose tree-specific operations as explicit actions. Automatic tab closing and suspension are deferred.

Agreed:

- Use tree-style tabs, inspired by [Horse Browser](research/horse-tab-organization-research.md). A [task](CONTEXT.md#language) is a native Chromium tab group. Tasks do not nest. Individual page tabs can have child tabs. Changed on 2026-10-11; see [ADR 0002](adr/0002-tasks-are-native-tab-groups.md).
- uw says "task" wherever it shows one: the task's menu, the tab menu, the new-tab menu, the menu bar, tooltips, and VoiceOver. Chromium's items that repeat a uw command are removed: "Add tab to new group" and "Group Tab" repeat "Move to new task", and "Remove from group" repeats "Move to Scratchpad". "Ungroup" becomes "Move tabs to Scratchpad", and "Add tab to group" becomes "Move to task". Chromium's own pages, such as history and tab search, keep their wording for now. Agreed on 2026-10-15.
- A closed task is not saved. Cmd+Shift+T brings back a task just closed, as it does a tab. Chromium's saved tab groups, their "Tab groups" menu, and the note that groups save automatically are removed, since the organizer starts tasks on its own and the saved list would only grow. Agreed on 2026-10-15.
- Preserve standard link navigation. Same-tab navigation stays in the current tab; a link opened as a new tab becomes a child of its source tab, including through Cmd-click or middle-click. Intent-based organization can later move it.
- Agreed: a lone new tab without a source link, such as Cmd+T, opens directly beneath the previously active tab as its sibling. Restored sessions and bulk opens keep their saved placement.
- Closing a regular tab closes only that page. Its children move up one level, preserving their order and remaining descendants. A separate "Close branch" action closes the selected branch and all its descendants.
- Ordinary parent-tab navigation keeps its children attached. A clear change of browsing task is required before the organizer moves the parent and promotes children that still serve the original task.
- When a parent tab changes activity and moves to another task, children that still serve the original task remain there. Promote those children one level in the old location, preserving their order and subtrees. This automatic reclassification moves the repurposed parent, unlike an explicit whole-branch extraction.
- Enable automatic grouping by default from the first AI grouping release. Organize new tabs and refine inactive groups.
- Automatically regroup tabs as task intent becomes clearer, including pulling a distinct task out into its own group.
- In automatic mode, creating a new task preserves candidate order and branches. Place it after every other task, just above the Scratchpad. Changed on 2026-10-13. Before, it went after the task the branch came from, or first among tasks.
- Merge two automatically created groups when they clearly represent the same task and neither has a manual adjustment. If either group has a manual adjustment, propose the merge instead.
- Split an automatically created group when its branches clearly represent separate tasks and it has no manual adjustment. If it has a manual adjustment, propose the split instead.
- Clear task intent can trigger a move immediately, including for the selected tab and other tabs in the current task group. Switching away is not a prerequisite. A move must preserve the selected tab, its page state, and keyboard focus.
- A confident classification into an existing task can take effect without waiting for the connected LLM. The [primary design rule](#principles) and manual adjustments still take precedence over automatic moves.
- How sure a model must be for automatic placement is the user's choice, through the [thresholds](plans/automatic-organization.md#thresholds) on the settings page. Short of that, leave the tab or branch where it is rather than making a plausible but incorrect move. Agreed on 2026-10-12. It replaces a fixed high-confidence rule.
- Without a configured LLM, leave tabs that Jev cannot confidently match to an existing task in Scratchpad until the user manually creates a task.
- When an LLM is configured, send background task-discovery requests for tabs or branches Jev leaves in Scratchpad. Keep them there while requests run; later clear results use the agreed automatic or user-directed task-creation rules.
- When the later excerpt-informed result clearly identifies a better task, correct the initial placement immediately unless the user made a manual adjustment after the initial result.
- Group primarily by inferred task or intent. A group can span websites, and tabs from the same website can belong to different tasks.
- Use separate groups for distinct immediate activities within the same project. For example, "Implement vertical tabs" and "Choose a grouping model" are separate groups, rather than one "Build uw" group.
- Keep brief detours ungrouped until continued browsing makes the separate task intent clear. An unrelated tab does not by itself require a new group.
- Never make a [hub](CONTEXT.md#language) into a task. A feed, an inbox, or a dashboard stays in the Scratchpad, or in a task it fits by name, while the tabs opened from it are filed on their own. Agreed on 2026-10-13.
- A regular tab opened from a shared pin starts ungrouped in the current window. A confident classification may place it immediately; the last active task alone does not determine its placement.
- For now, when a tab clearly belongs to an existing task but has no clear parent page, append it at that task group's top level. Nest it under a page only when the parent relationship is clear, such as a link explicitly opened from that page.
- Organize one regular tab tree per workspace, shown in every window on it; see [Workspaces](#workspaces). Changed on 2026-10-29: each window had its own tree. [Pinned tabs](#pinned-tabs) have their own shared-state requirements.
- Each regular tab has one home position in the hierarchy. Separate tabs showing the same page may belong to different task groups.
- Preserve the user's expanded/collapsed group choices. The organizer does not change them when tasks switch.
- When a selected tab moves into a collapsed task group, keep the group collapsed and show a brief destination cue rather than changing the user's expansion choice.
- Let the user change grouping and ordering. Manual placement takes priority while the tab serves the same browsing task. A later clear change of activity makes the tab eligible for automatic regrouping again; ordinary navigation within the same task preserves the correction. Exception: a child tab the user nested by hand stays under its parent until the user moves it or the parent closes. Agreed on 2026-10-15. The user can also keep any tab where it is with Keep here, agreed on 2026-10-19; see [Automatic organization](plans/automatic-organization.md#agreed-scope).
- For the first release, treat a manual correction as applying only to that tab or branch while its activity remains the same. Do not infer a broader placement rule from one correction.
- Provide Tidy, an on-demand AI pass that files the Scratchpad's tabs into tasks. Changed on 2026-10-24: it was a "Reorganize all tabs" button over every tab in the window. See the [Scratchpad buttons plan](plans/scratchpad-buttons.md).

### Organization latency

Agreed: the initial whole fast path must complete within **200 ms**, measured from title, URL, and source-tab relationship becoming available to visible placement. This includes preprocessing, queuing, classification, and applying an eligible result to the interface. Apply eligible moves immediately, without adding a delay for the user to finish interacting with the sidebar.

Agreed for the first remote release in [Automatic organization](plans/automatic-organization.md): Jev runs as background correction. A valid late result may still organize its branch. The 200 ms visible-placement fast path remains a future performance milestone for that release, not a claim the remote integration makes.

Reference hardware, representative input sizes and task counts, and cold-start expectations still need definition. The latency requirement is not yet benchmarked.

### Scratchpad

Agreed: each window has an always-visible Scratchpad section below every task group, at the foot of the tab list. Changed on 2026-10-13. It used to sit above the tasks. It contains every ungrouped regular tab and candidate branch, rendered as a nested tree. A confident placement moves a whole Scratchpad branch into the matched task by default. Tabs leave it only through confident automatic placement, an accepted task proposal, Tidy, or a manual move. Scratchpad is not a task group and does not participate in task ordering or automatic task restructuring.

Agreed on 2026-10-24: the Scratchpad heading has Tidy and Clear, as Arc has. Clear closes the Scratchpad's tabs. See the [Scratchpad buttons plan](plans/scratchpad-buttons.md).

### User-directed mode

Agreed: offer an optional mode where the organizer proposes new task boundaries for the user to accept or reject. Routine tab placement within established tasks remains automatic.

Proposed presentation: an inline task proposal with "Create task" and "Not a task" choices.

Agreed: while a task proposal awaits a decision, its candidate tabs remain ungrouped and keep their current positions.

Agreed: choosing "Not a task" leaves candidate tabs ungrouped and suppresses the same proposal until their activity clearly changes.

Agreed: choosing "Create task" creates a root-level task group at the earliest candidate tab's current position. Move candidates into it in their existing relative order.

Agreed: when a candidate tab has children, accepting its proposal moves the whole branch into the new task group and preserves its hierarchy.

Agreed: an ignored task proposal stays as a small inline Scratchpad row until the user chooses "Create task" or "Not a task." It does not rearrange tabs or interrupt browsing.

### Manual task extraction

Agreed: provide a "Move to new task" action in the tab/branch menu and for a selection of tabs. It creates a separate task group in the same window and counts as a manual adjustment. A selected parent moves with all its descendants by default; selecting individual child tabs allows part of a branch to be extracted.

Preserve loaded pages, the selected tab, and the moved branches' internal hierarchy. The action takes effect without waiting for an AI decision. Place the new task group after every other task. Changed on 2026-10-13. Before, it went immediately after the source task.

### Task names

Agreed: once a task is known, give its group a short, action-oriented name describing the immediate activity, such as "Benchmark GLiNER2.5". Revise an automatically generated name only when the task clearly changes. Users can edit names; a user-edited name takes priority over automatic renaming.

Agreed on 2026-10-12: the model that decides placement also decides when a task needs a name or a new one. The language model writes the name and is asked only then. See the [name check](plans/automatic-organization.md#name-check).

Agreed on 2026-10-13: a new task does not wait for its name. Its tabs move at once into a task with the provisional name, and the task takes its name when the language model answers. Until then a spinner shows on the task's row.

### Group order

Agreed on 2026-10-13: tasks are listed in the order they were started, oldest first. A new task comes last, whoever started it.

Agreed: existing sibling task groups keep their relative order when the user switches tasks. Activity or recency does not move a group to the top. Users can drag groups to reorder them; intent-based regrouping can still move tabs or branches when the task becomes clear.

### Organization undo

Agreed: one Undo reverses a complete organization action, including task creation and its associated tab moves. Preserve loaded pages and the current selection. Treat the restored placement as a manual correction while the activity remains the same.

### Organization controls

Agreed: expose organization controls through a custom button menu inside the tab sidebar, so manual organization remains usable while automatic behavior is developed.

Built: an "Organize" pill at the foot of the sidebar. It opens a card with a status line, one pause row, "Undo Move", "Show Activity", and "Settings…". A task's ⋮ button and a right-click on its row open the task's card, and a task is renamed on its own row. See the [sidebar menus plan](plans/sidebar-menus.md). Tab and branch actions such as "Move to New Task" and "Close Branch" are in each tab's right-click menu and the menu bar. Proposed: the automatic/user-directed mode choice joins the Organize card when it is built. Tidy and Clear are on the Scratchpad heading.

Agreed on 2026-10-15: the [new button](plans/keyboard-and-mouse.md#new-button), a "+" with a menu, sits at the right end of the sidebar's foot, across from Organize, and replaces the "+" under the tab list.

Agreed: provide global pause choices for automatic organization: "Pause for 1 hour", "Pause until next launch", and "Pause indefinitely", plus "Resume automatic organization". Pausing stops automatic placements and task proposals across all windows, including application of pending automatic results. Manual tree controls and Tidy remain available. Tidy still targets the current window.

While automatic organization is off, the settings page hides the pause choices and the menus gray them out. Agreed on 2026-10-15.

The first release supports these three pause choices only. The one-hour pause expires automatically. "Until next launch" resets when uw next starts. "Indefinitely" remains in effect until the user resumes automation.

### Intent context

Agreed: local task inference may use tab titles, URLs, source-tab relationships, and short excerpts of readable page text. Run an initial classification from the title, URL, and source relationship, then re-evaluate when a readable-text excerpt becomes available. Neither pass may block browsing.

Agreed: when the user selects a remote provider for tab organization, requests may include titles, URLs, short relevant page excerpts, current task names, and local classifier findings for the tabs being evaluated.

Agreed: Jev receives no intent context until the user enables Jev tab classification.

Agreed: after opt-in, Jev receives title, URL, and source-tab relationship for the initial pass, then a short readable-text excerpt for the later pass. Show this exact scope during enablement.

Agreed for the [automatic organization MVP](plans/automatic-organization.md): each classification request contains titles, URLs, source-tab relationships, current task names, and a short readable-text excerpt. The settings page shows this exact scope before the user enables automatic organization.

## Agent automation

uw's agent carries out requests in the browser. A chat runs a request when the user asks; a scheduled chat runs one again on a schedule.

### Chats

Agreed on 2026-10-14 as v2, after the minimum usable version: a built-in agent browses for the user in a [chat](CONTEXT.md#language). It handles any request it can carry out, including retrieving information and taking actions that change things.

- A chat is a regular tab. The agent's tabs are its child tabs, open in the background, and never take the selection or focus.
- The agent asks for [approval](CONTEXT.md#language) before each change on a site and each sign-in, in the default [approval mode](CONTEXT.md#language), Auto. Ask first also asks before every click and typing; Full auto never asks.
- Agreed on 2026-10-20: the agent can run commands on this Mac in a [terminal tab](CONTEXT.md#language) under its chat. In Auto, a [command check](CONTEXT.md#language) asks only before potentially dangerous commands, as Claude Code's auto mode does, and every mode asks before a command reads a [sensitive file](CONTEXT.md#language).
- Agreed on 2026-10-21: [connectors](CONTEXT.md#language) give the agent tools from MCP servers, and the user can start a [skill](CONTEXT.md#language) by typing `/` and its name. See the [connectors plan](plans/connectors.md). Agreed on 2026-10-22: a connect dialog finds a service and adds its connector and skills with one click and a sign-in. See the [connect dialog plan](plans/connect-dialog.md).
- Agreed on 2026-10-24: a chat shows how full its model's context is and what the chat has cost. See the [context and cost plan](plans/context-and-cost.md).
- [Outside agents](CONTEXT.md#language) such as Claude Code use the same tools later, through `uw mcp serve`.

Example use cases:

- Check whether the latest deployment for the user's personal organization on Vercel succeeded.
- Retrieve logs for the most recent crash on Railway.
- Cancel unused subscriptions and request refunds.

The [agent chats plan](plans/agent-chats.md) has the full scope, releases, and stages.

### Scheduled chats

Agreed on 2026-10-14: a page watch is a [scheduled chat](CONTEXT.md#language). The watch rules below carry over to every scheduled chat. A step that needs approval waits and sends a notification.

Agreed on 2026-10-22: a scheduled chat is any request with a schedule, and need not be about one page. Watching a page starts one from that page, which becomes a [watched page](CONTEXT.md#language).

Agreed schedules, on 2026-10-22: every hour or every 2, 3, 4, 6, or 12 hours; every day, or weekdays, at a time; every week on a day at a time; and once at a date and time, after which the chat keeps no schedule. Hourly is the shortest. A schedule follows the Mac's current time zone, shown as a quiet line under Repeat with Change, since most people never change it; one picked with Change stays fixed. 9:00 stays 9:00 across a daylight-saving change. No custom cron syntax. To watch more closely than hourly, use a trigger.

Agreed on 2026-10-22, after Claude Code's background commands and Monitor tool: a [trigger](CONTEXT.md#language) is a command left running out of sight that wakes its chat once when it ends, or once for each line it prints. Lines that come close together are one wake. A trigger that prints too much is stopped, and the chat says so.

| | In a chat | In a scheduled chat |
| --- | --- | --- |
| Started by | The agent, under the usual command rules | The user, saving it on the schedule card |
| A wake | Reaches the agent as a [steer](CONTEXT.md#language) does | Starts a run |
| Lasts | Until the agent or the user stops it, uw quits, or 24 hours pass | Until the user removes it; it starts again when uw launches |

Agreed on 2026-10-22, so a trigger the agent starts in a chat costs little when it is forgotten, with nothing for the user to do: it wakes the chat at most once every 2 minutes, and lines printed in between come with the next wake. After its first hour it wakes at most once every 15 minutes, and each wake is handled as a run by a subagent on the runs model, the one last picked on a schedule card or else the chat's own; that run adds one line and wakes the chat's model, and notifies, only when it finds something worth telling. After 24 hours it stops by itself, and the chat says so and that a schedule card can keep it going. A trigger saved on a schedule card has none of these limits.

Agreed use cases: track a price on a page, look for news on a topic, and send a regular update.

Agreed on 2026-10-22: there are three ways to set one up, and all use the same schedule card. On a chat, "Run this chat again". On a page, "Watch this page", which also asks what to watch for; the command bar has the same command. In words in a chat, such as "check this every morning at 9": the agent fills in the card and the user saves it. The agent never saves a schedule on its own. Agreed on 2026-10-22: a new schedule runs once as soon as it is saved, so a watch has its first value at once and a page it cannot read shows at once.

Agreed on 2026-10-22, with the "Watch this page" frames on the canvas: an eye beside the star in the address bar both starts a watch and shows one, outline until the page is watched and filled after. Its card offers fixed "Watch for" chips, Price drops with an optional "Only below" price, Back in stock, New items, and Anything worth knowing, or the user's own words; the pick becomes the chat's request. A watched page matches by its address without the part after # and common tracking parameters such as `utm_` and `ref`. The tab's sidebar row shows nothing extra.

Agreed on 2026-10-28: a chat can set up a watch. When the user asks in words, such as "tell me when this desk drops below $500", the agent fills in the schedule card with the page's address and, when the ask matches one, a "Watch for" chip. The request stays in the user's words; the chip does not rewrite it. The card then reads "Watch this page" and shows the page, with its title and icon found as on the Scheduled page, and what it watches for. Neither can be changed on the card; the user asks the agent instead. Saving makes this chat watch the page, in place of any page it watched before. The card saves what it shows, even after the user edits the request.

Agreed on 2026-10-28: several chats can watch one page, such as different topics on bbc.com. When the agent fills in a page that other chats watch, it is told which ones, so it can ask whether to change one of them instead. With several, the eye's tooltip reads "Watching this page for 2 things", and its card opens on a list with one row per watch; a row opens that watch's card, with a back arrow. The list and a single watch's card both end with "Watch for something else", which opens the chips to watch the page again in a new chat.

Agreed access: the first watch release supports public pages and signed-in pages through the user's existing browser session. If sign-in expires, show "Sign in required" so the user can sign in manually. Automatic sign-in is deferred.

Agreed failures, on 2026-10-22: a failed run is one line in the chat, such as `9:00 · Didn't finish · fully.com didn't load`, and the Chats list shows the warning icon until a run succeeds. The first failure after a success notifies with the reason; later ones stay quiet until a run succeeds again. "Sign in required" notifies once and the strip shows it with Sign in, which opens the site in the user's window; the next run tries again. Mute silences failures too.

Revised on 2026-10-22: three failed runs in a row pause the schedule, with a notification and the reason on the strip beside Resume. Every failure counts, such as a site that does not load or answer, "Sign in required", or a run that ends without a report, except two: a failure to reach the model, such as the provider being down or rejecting the key, and a lost connection, such as the Mac being offline. Those do not count and do not end a streak.

Agreed reporting, revised on 2026-10-22: each scheduled chat notifies in one of two ways.

| Mode | Behavior |
| --- | --- |
| [Change alerts](CONTEXT.md#language), default | Notify when a run finds what the user asked to hear about. A run that finds nothing new stays in the chat's history, unless "Tell me even when nothing changes" is on; then it sends a short "No change" with the time it checked. |
| [Regular updates](CONTEXT.md#language) | Notify after every run with what it found. |

A summary that combines several scheduled chats comes later.

Agreed delivery: keep results and history in uw. Send native macOS notifications. Let users mute notifications per scheduled chat while runs and history continue. Since 2026-10-29 the notifications stay in Notification Center after uw quits, per the [agent chats plan](plans/agent-chats.md#agreed-scope).

Agreed availability: scheduled chats run while the uw app is running, even with all windows closed; confirmed on 2026-10-22, no window needs to be open. Quitting uw stops them. After runs are missed because uw was quit or the Mac was asleep or off, make one [catch-up run](CONTEXT.md#language) when uw can next run. Show the actual run time.

Agreed on 2026-10-22: runs happen out of sight, whether or not a window is open. A run never adds tabs to the user's windows and never touches the user's own tab of a watched page; it opens its own. Its tabs close when it ends, and the chat keeps the answer and a card of each page. Opening the chat, from its notification or the Chats list, shows it in the user's window; a run waiting for approval brings its waiting tab along. A run under way when uw quits is marked "Stopped before it finished".

Agreed on 2026-10-22: a scheduled chat stays one conversation. Each [run](CONTEXT.md#language) is a subagent that starts fresh with the request, the last few results, and the [tracked values](CONTEXT.md#language). It adds one message to the chat, with its steps folded under it. A run with nothing new is one quiet line, such as `9:00 · No change · $129`. Each run saves its tracked values, with the time, on this Mac. A reply in the chat goes to the main agent, which sees every result and the tracked values. [ADR 0013](adr/0013-each-run-of-a-scheduled-chat-is-a-subagent.md) records why.

Agreed on 2026-10-22: a run uses its scheduled chat's [approval mode](CONTEXT.md#language), shown on the schedule card, which turns amber for Full auto. One run of a scheduled chat goes at a time. A run waiting for approval waits until the user answers or stops it; the times due meanwhile are skipped, and the chat then says "Skipped 3 runs while this waited for you."

Agreed on 2026-10-22: no cap on runs. Runs of different scheduled chats all go at once, and there is no spending cap.

Agreed on 2026-10-22: the schedule card has a Model row, with the chat's model and effort chips, for the model runs use, such as a cheaper one like DeepSeek's `deepseek-flash`. It starts on the model last picked on a schedule card. Replies in the chat use the chat's own model. Only models the model catalog lists with tool calls can run, since a run ends with `report`.

Agreed on 2026-10-22: the model judges whether something happened; uw does no page comparison of its own. A run ends by calling a `report` tool with whether it found what the user asked to hear about, a message, and its tracked values, such as `price: 99 USD`. uw acts only on that: it notifies per the chat's setting, saves the values, and writes the run's line. A run that ends without a report counts as "Didn't finish", never as no change. For runs, these settings replace the notification a chat sends when it answers.

Agreed charts, on 2026-10-22: a tracked value that is a number gets a line chart, and a yes or no value a strip of colored spans; text values are not charted. The watched page's card shows the last 14 days. The schedule strip's "Chart" button opens a larger chart above the conversation, with 7 days, 30 days, and All; a point shows its run's line, and a click scrolls to that run. Notifications and the Chats list stay text. The first run names each value and its unit, and later runs reuse them. Values are kept until the chat is deleted; removing the schedule keeps them.

Agreed management, on 2026-10-22:

- A scheduled chat's page shows a strip under its title, such as `Every day at 9:00 · Next: tomorrow 9:00 · Last: No change, $129`, with Edit, which opens the schedule card, Pause, and Mute. A running trigger shows there with Stop.
- The Chats list shows a clock and the next time.
- Pause stops runs and triggers. Resuming does not catch up on the paused time.
- Deleting the chat deletes its schedule. Removing the schedule keeps it as an ordinary chat.

Agreed on 2026-10-29: [Check now](CONTEXT.md#language) starts a run of a scheduled chat at once.

- It is on the strip, at the end of a single watch's card's last-run line, and among a Scheduled page row's hover buttons. A page with several watches has it on each watch's card, not on their list. A chat with only a trigger has none, and the command bar has no Check now command.
- It hides while a run is under way or waits for approval; "Running now" shows instead.
- It is an extra run: the schedule's next time stays. Otherwise it is like any run: it notifies per the chat's settings, counts toward the failure pause, and shows in the chat with no mark.
- On a paused schedule it runs once, and the schedule stays paused.

Agreed on 2026-10-28: the Scheduled page, `uw://scheduled`, replaces the Scheduled filter of the chat page's chat list.

- It lists each scheduled chat once, in two sections: "Watched pages" first, then "Scheduled chats", which also holds chats with a running trigger, as the filter did. An empty section is left out. The header has Pause all.
- A watched page's row shows the page's icon and title, what it watches for, the latest value, the last run, and the next run. The last run reads Waiting for approval, Sign in required, Paused, or Didn't finish when one applies. The title and icon come from an open tab of the page, else from history, else the site.
- A click on a watched page's row opens the page, or goes to its tab if one is open. A click on any other row opens the chat. On hover, a watched page's row offers Open chat, and every row Pause or Resume. Edit, Mute, Stop watching, and Delete stay on the chat page and the watch card.
- The Inbox's "See all" and the "Show Scheduled" command open it.
- Its header has Pause all, New scheduled chat, and Watch a page, two more ways to set one up. Watch a page opens the watch card with an Address field above "Watch for"; the page need not be open. An address already watched shows a note with Open, such as "Watched for price drops · Open", and can still be watched for something else. New scheduled chat opens the schedule card with a Request field; saving makes a chat whose first message is the request. Both run at once when saved, and the page stays open.

Agreed on 2026-10-28: every notification a scheduled chat sends is also kept in uw as an [alert](CONTEXT.md#language) in the [Inbox](CONTEXT.md#language).

- The Inbox button sits at the top of the sidebar in every normal window, the floating sidebar included, and never in private windows. It shows how many alerts are unseen. Opening the Inbox marks them all seen, and clicking an alert's notification marks that one seen.
- An alert shows the page's icon, or a clock for a chat with no page, the chat's title, what the notification said, and the time.
- A click on a watched page's alert opens the page, or goes to its tab if one is open, with Open chat on hover. Any other alert opens its chat. "Waiting for your approval" always opens the chat. A click on the notification itself still opens the chat.
- Alerts stay until Clear all or until their chat is deleted. Stopping a watch or removing a schedule keeps them. An approval's alert goes once the step is answered or stopped.

### The uw command

Agreed on 2026-10-24: the [`uw` command](CONTEXT.md#language) runs chats, manages connectors, and changes settings from a terminal, a script, or an outside agent. In a terminal, a chat looks and asks for approval as Claude Code does; with no terminal, it never waits for input. The user installs it from uw's settings. The [uw command plan](plans/uw-command.md) has the details.

## Integrations

Integrations bring tools from the user's computer into uw. The terminal tab is the first. A file explorer may follow; nothing about it is decided.

### Terminal tabs

Agreed on 2026-10-14, as a later addition outside the minimum usable version:

- A [terminal tab](CONTEXT.md#language) is the user's own shell in a regular tab, so it can sit in the same task as the pages it belongs with.
- The organizer files a terminal tab like any other tab. A model gets its title and the shell's folder, never what the shell printed. Changed on 2026-10-14. Before, the organizer left terminal tabs alone.
- In the first version, text reaches the shell only from the user's keyboard, paste, or drop. A terminal's address never carries a command. Agreed on 2026-10-20: the agent's commands reach terminal tabs it opened, never ones the user opened.
- Closing a busy terminal asks first, however it is closed.

The [terminal tab plan](plans/terminal-tab.md) has the full scope and what is left out.

Agreed future direction: other parts of uw, and pages the user allows, may ask to run a command in a terminal, and each needs the user's approval. The prompt and what uw remembers are open.

Agreed future direction: explore [full libghostty in a native view](research/native-libghostty-terminal.md) once the first version works.

## Browser experience

Agreed: use [automatic tab organization](#automatic-tab-organization) to help track the current task and its tabs. An earlier proposal to track tasks with Arc-style workspaces was dropped, since it did not solve that problem. Workspaces came back on 2026-10-21 for a different job; see [Workspaces](#workspaces).

### Workspaces

Agreed on 2026-10-21: [workspaces](CONTEXT.md#language) work as Arc's spaces. The user swipes between them in the sidebar, each has its own theme, and several can share one [profile](CONTEXT.md#language)'s logins and history. Switching replaces the window with its partner for that workspace. Tasks still organize the tabs inside a workspace. The [workspaces plan](plans/workspaces.md) has the details.

Agreed on 2026-10-29, not built: every window showing a workspace shows its tabs, as in Arc, with one live page per tab that moves to the window that selects it, as pins do. Closing a window closes no tabs. The [shared tabs plan](plans/shared-tabs.md) has the details, and [ADR 0024](adr/0024-a-workspaces-tabs-show-in-every-window.md) the reasons.

Proposed: suspend inactive tabs when appropriate and restore sessions reliably.

Agreed on 2026-10-14: every command has a menu bar entry and a key, and the tab tree works with the mouse alone. Tasks do not nest: a [tab branch](CONTEXT.md#language) is the only level below a task. Branches collapse, and dragging nests them. The [keyboard and mouse plan](plans/keyboard-and-mouse.md) has the keys and the details.

Agreed on 2026-10-14: a [command bar](CONTEXT.md#language) replaces typing in the address bar. Cmd+T, Cmd+L, and a click on the address open it. It finds commands, tasks, open tabs, history, bookmarks, and the web, and offers "Ask uw" to start a chat. Text typed there reaches a model only when the user asks for one. Widened on 2026-10-15: results are ranked by use, Cmd+Return sends the text to a chat, Option+Return runs it in a terminal, and sums can mix units and currencies. See the [command bar plan](plans/command-bar.md) and [ADR 0005](adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md).

### Pinned tabs

Agreed: show pinned tabs as compact icons at the top of the sidebar, inspired by Arc's Favorites and Chrome's vertical-tab pin area. Each pin has a saved home URL it returns to when reset.

Agreed: each pin represents one continuing live page session shared across windows. Selecting it in another window continues its current navigation and in-progress page state, including an unfinished form or comment. Ordinary navigation does not replace the saved home URL.

Agreed: only one window displays a pinned page session at a time. Selecting the pin in another window transfers the live page there. The previous window retains a hazed, inactive pinned view rather than a second live copy: a blurred, dimmed picture of the page as it left, with "Open here" in the middle. A click anywhere on it transfers the live page back. Switching workspace carries the live page along when the window that comes into view has the pin selected, since a hidden window displays nothing. The picture and the workspace rule were agreed on 2026-10-22; see the frame "Pins: the live page in another window" in the [workspaces plan](plans/workspaces.md#design).

Agreed: pins are shared across all windows in one browser profile and separate between profiles. Since 2026-10-21 that includes every [workspace](#workspaces) on the profile.

Agreed: Cmd-W while the pinned page itself is active is a no-op. It does not close the page or window, remove the pin, return the page to its home URL, or affect its state in other windows.

Agreed: double-clicking a pinned icon or choosing "Reset to home" from its context menu returns the pinned page to its saved home URL.

Agreed: "Replace home URL with current" changes the saved home URL without navigation. "Edit home URL" changes the saved home URL and immediately navigates the live pinned page there.

Agreed: a pin's site is the saved home URL's scheme and registrable domain. Same-site navigation stays in the pinned page session. A link that would leave the site opens in a temporary Peek preview. Closing the preview leaves the pinned page unchanged; promoting the preview creates a regular tab in Scratchpad.

Agreed: unpinning removes the icon and inactive pinned views across the profile. The live page remains open as a regular tab in the window currently displaying it. When no window holds it, it opens as a regular tab in the window the pin was unpinned from (agreed 2026-10-22).

Agreed: closing the window that displays a pinned page detaches its live session from that window without discarding it. Selecting the pin in another open window restores the same page state there.

Agreed: pinned page sessions remain live when the last browser window closes while uw remains running. Opening a new window restores their exact state, subject to measured resource limits.

Agreed: every launch after a quit restores the last session without asking, as Arc does. Pinned pages come back at their last live URLs, with Chromium-recoverable session state, alongside regular tabs. Chromium's "On startup" setting still lets the user start blank. After a crash, Chromium's "Restore pages?" prompt asks first. Its "Always restore after a crash" checkbox starts unticked; once ticked, later crashes restore without asking. uw follows Chromium's own Session Restore Infobar work and drops its parts once Chromium ships them. Changed on 2026-10-18: before, a "Restore tabs" popup offered restoration. The crash checkbox was agreed on 2026-10-20. See [Restore](plans/native-tab-tree.md#restore).

### Mini player

Agreed on 2026-10-24: a picture-in-picture video becomes a [mini player](CONTEXT.md#language), as in Arc. It docks at the bottom of the sidebar, so the user can browse other tabs while it plays, and floats in its own window while uw is in the background. Switching away from a playing video starts one. Agreed on 2026-10-25: a small window a site opens itself, such as a Meet call, docks too, as a [live window](CONTEXT.md#language) that never pauses; [playback controls](CONTEXT.md#language) show for a background tab playing sound; and the user can choose not to dock, or to turn picture in picture off. The [mini player plan](plans/mini-player.md) has the details.

## AI connections

Agreed setup: when the user first enables an AI feature, ask them to choose a connection mode. Ordinary browsing works without AI setup.

Agreed: require an explicit "Enable Jev tab classification" choice before sending intent context to Jev. Until it is enabled, leave tabs in Scratchpad rather than automatically classifying them.

| Mode | Expected behavior |
| --- | --- |
| Managed local model | uw recommends a model for the device, shows the download size, and downloads and manages it after the user chooses it. |
| Ollama | Connect to an Ollama endpoint and choose an available model. |
| Online provider | Support OpenAI API, Anthropic API, OpenRouter, Vercel AI Gateway, and custom endpoints in any format uw speaks. |

Agreed on 2026-10-15 for v2.1: the user saves each [provider](CONTEXT.md#language) once, with its key, and every model choice picks from them: the language model, the decision model, and the agent model. Each chat can switch its model and effort. See the [AI providers plan](plans/ai-providers.md). Agreed on 2026-10-21: every profile shares the same providers, and each profile picks its own models.

Agreed on 2026-10-11: settings also hold an optional decision model beside the language model. It can be Jev, the OpenAI Decisions API, or an endpoint compatible with the OpenAI Decisions API. It decides where a tab goes; the language model names new tasks. See [ADR 0003](adr/0003-decision-model-beside-the-language-model.md).

Show the active provider and model. Explain which features a model supports. If it lacks a required capability, offer a supported choice rather than silently switching providers.

Local mode keeps inference local. Sending page content to a remote provider requires a user-selected remote feature or automation.

## Prompt-built extensions

Agreed scope: support broad website customization, including layout changes, theme overrides, and changes to site behavior. The limits of customization remain open.

Agreed creation methods:

- Describe a page-level change, such as "give this site a dark theme and make articles wider."
- Select an element and prompt with that selection as context, such as "hide this sidebar" or "make this table sortable."

Connect to coding tools such as OpenCode, Claude Code, and Codex to generate a browser extension. These connections are separate from the browser's inference provider settings.

Agreed persistence: enabled customizations reapply across visits and browser restarts. A per-site list lets users edit, disable, or remove them.

The proposed flow is prompt, generate, inspect the change and site permissions, try it on the target page, then enable it. Prompt refinement and version rollback remain proposed.

Keep generated source available to the user. Use Chromium's extension model where supported by the chosen integration.

## Ad blocking and privacy

Agreed: the first clutter-removal release includes an options page with separate controls for:

- Ad blocking.
- Tracker blocking.
- "Apply cookie preferences": when enabled, default to rejecting optional cookies and keeping necessary ones.
- Social button removal.
- Newsletter-popup dismissal.
- Autoplay blocking.
- Hiding floating chat widgets and sticky promotions.
- Suppressing site notification requests.

Agreed: support per-site overrides for cleanup settings.

Agreed defaults: enable ad blocking, tracker blocking, and "Apply cookie preferences". All other cleanup controls start disabled.

How per-site overrides take precedence remains open.

Agreed on 2026-10-29: uw sends Google nothing about what the user does in the browser. Google receives data only when the user uses a Google service, such as searching with Google, translating a page, or opening a Google site, and then only what that service needs. Background requests that keep the browser working, such as component updates and push messaging, stay; they carry no browsing data. Private windows follow the same rule. The [Google requests plan](plans/google-requests.md) lists each request and what uw does with it, and the Help page [Privacy: what uw sends](https://uwbrowser.com/help/privacy) lists everything uw sends, to Google and everyone else.

Other proposed privacy behavior:

- Use maintained filter lists for blocking.
- Show what was blocked.
- Provide private browsing and clear controls for cookies, site permissions, and browsing data.
- Keep private sessions out of persistent AI history and scheduled watches. Explicit AI requests in private sessions follow the selected provider's data handling.
- Make any telemetry opt-in and keep page content out of it.

## Download name suggestions

Suggest readable filenames using the original name and relevant page context. For example, suggest `electricity-bill-2026-09.pdf` instead of `download.pdf` when the page supports that description.

Agreed naming choices:

- "Always use recommended names": apply recommendations automatically across all sites.
- "Use recommended names for this site": apply recommendations automatically for that site.
- When neither option applies, ask on each download whether to use the recommended name.

Other proposed behavior: allow editing a suggestion or keeping the original name. Preserve the real file extension, sanitize invalid characters, and handle name collisions. Keep the original filename if AI is unavailable or too slow. Naming must not block the download.
