# Product

The headline features are agreed. Detailed feature behavior below is proposed unless marked agreed.

## Goal

Make everyday browsing easier to manage, automate, and customize.

## Audience

Agreed: build for the project owner's daily browsing first. A broader target audience remains undecided.

## Minimum usable version

Agreed: the first version targets macOS desktop. Chrome-like everyday browsing with vertical tabs is enough to start daily use. Other planned features can follow.

Agreed: keep uw branding minimal and use native vertical tabs by default. Preserve the user's explicit choice to use horizontal tabs.

Agreed: manual setup is acceptable for the first usable version. Automatic browser-data import is not required for that version.

Agreed: performance and stability are basic usability requirements. Crashes, disruptive slowdowns, and broken daily workflows block acceptance. Later features must preserve that baseline.

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

## Browser experience

Agreed: use [automatic tab organization](#automatic-tab-organization) to help track the current task and its tabs. This replaces the earlier Arc-style workspace proposal, which did not solve the user's task-tracking problem.

Proposed: suspend inactive tabs when appropriate, restore sessions reliably, and provide keyboard shortcuts for common browsing actions.

### Pinned tabs

Agreed: show pinned tabs as compact icons at the top of the sidebar, inspired by Arc's Favorites and Chrome's vertical-tab pin area. Each pin has a saved home URL it returns to when reset.

Agreed: each pin represents one continuing live page session shared across windows. Selecting it in another window continues its current navigation and in-progress page state, including an unfinished form or comment. Ordinary navigation does not replace the saved home URL.

Agreed: only one window displays a pinned page session at a time. Selecting the pin in another window transfers the live page there. The previous window retains a hazed, inactive pinned view rather than a second live copy. Selecting that inactive view transfers the live page back.

Agreed: pins are shared across all windows in one browser profile and separate between profiles.

Agreed: Cmd-W while the pinned page itself is active is a no-op. It does not close the page or window, remove the pin, return the page to its home URL, or affect its state in other windows.

Agreed: double-clicking a pinned icon or choosing "Reset to home" from its context menu returns the pinned page to its saved home URL.

Agreed: "Replace home URL with current" changes the saved home URL without navigation. "Edit home URL" changes the saved home URL and immediately navigates the live pinned page there.

Agreed: a pin's site is the saved home URL's scheme and registrable domain. Same-site navigation stays in the pinned page session. A link that would leave the site opens in a temporary Peek preview. Closing the preview leaves the pinned page unchanged; promoting the preview creates a regular tab in Scratchpad.

Agreed: unpinning removes the icon and inactive pinned views across the profile. The live page remains open as a regular tab in the window currently displaying it.

Agreed: closing the window that displays a pinned page detaches its live session from that window without discarding it. Selecting the pin in another open window restores the same page state there.

Agreed: pinned page sessions remain live when the last browser window closes while uw remains running. Opening a new window restores their exact state, subject to measured resource limits.

Agreed: after the user quits uw, the next launch opens a new tab and offers a "Restore tabs" popup. Do not automatically restore the prior session. Accepting restoration restores pinned pages at their last live URLs and Chromium-recoverable session state alongside regular tabs. Dismissing it leaves the new tab open; pins remain available in the sidebar.

## AI connections

Agreed setup: when the user first enables an AI feature, ask them to choose a connection mode. Ordinary browsing works without AI setup.

Agreed: require an explicit "Enable Jev tab classification" choice before sending intent context to Jev. Until it is enabled, leave tabs in Scratchpad rather than automatically classifying them.

| Mode | Expected behavior |
| --- | --- |
| Managed local model | uw recommends a model for the device, shows the download size, and downloads and manages it after the user chooses it. |
| Ollama | Connect to an Ollama endpoint and choose an available model. |
| Online provider | Support OpenAI API, Anthropic API, OpenRouter, Vercel AI Gateway, and custom OpenAI-compatible endpoints. |

Show the active provider and model. Explain which features a model supports. If it lacks a required capability, offer a supported choice rather than silently switching providers.

Local mode keeps inference local. Sending page content to a remote provider requires a user-selected remote feature or automation.

## Scheduled page watches

Let users choose a page, describe what to watch, and set a schedule. Support daily, weekly, and specific-time schedules with an explicit timezone.

Agreed use case: check sites for updates, such as price changes.

Agreed access: the first watch release supports public pages and signed-in pages through the user's existing browser session. If sign-in expires, show "Sign in required" so the user can sign in manually. Automatic sign-in is deferred.

Agreed reporting: let users choose a mode per watch.

| Mode | Behavior |
| --- | --- |
| Condition-based alerts, default | Notify when the user's condition matches. |
| Scheduled summaries | Report watched-page updates on a chosen schedule. Summaries can cover multiple watched pages. |

Agreed delivery: keep results and history in uw. Send native macOS notifications for matching alerts and scheduled summaries. Let users mute notifications per watch while checks and history continue.

In alert mode, successful checks with no matching result stay in watch history without interrupting the user.

Agreed availability: watches continue while the uw app is running, even with all windows closed. Quitting uw stops checks. After checks are missed because uw was quit or the Mac was asleep or off, run one catch-up check when uw can next run. Show the actual check time.

Other proposed behavior:

- Check the page and compare it with the previous successful result.
- Show the last result, next scheduled run, and any failure.
- Allow editing, pausing, and deletion.

Use ordinary page comparisons where sufficient and AI when interpretation is needed.

## Delegated browser tasks

Agreed future direction: let an agent take control of the browser for open-ended automation. Support any browser task the agent can perform, including retrieving information and taking actions that change things. Tasks are not limited to a predefined catalog.

Example use cases:

- Check whether the latest deployment for the user's personal organization on Vercel succeeded.
- Retrieve logs for the most recent crash on Railway.

How users define tasks and how agents report progress and completion remain open.

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

Other proposed privacy behavior:

- Use maintained filter lists for blocking.
- Show what was blocked.
- Provide private browsing and clear controls for cookies, site permissions, and browsing data.
- Keep private sessions out of persistent AI history and scheduled watches. Explicit AI requests in private sessions follow the selected provider's data handling.
- Make any telemetry opt-in and keep page content out of it.

## Automatic tab organization

Agreed priority: this is the first major addition after the [minimum usable version](#minimum-usable-version).

Agreed scope: the first tab-management release focuses on organization. Infer the user's browsing task, then decide how to organize the tabs. Preserve familiar Chromium behaviors where possible and expose tree-specific operations as explicit actions. Automatic tab closing and suspension are deferred.

Agreed:

- Use tree-style tabs with nested organization, inspired by [Horse Browser](horse-tab-organization-research.md). Task groups can contain tabs and nested task groups. Individual page tabs can have child tabs.
- Preserve standard link navigation. Same-tab navigation stays in the current tab; a link opened as a new tab becomes a child of its source tab, including through Cmd-click or middle-click. Intent-based organization can later move it.
- Closing a regular tab closes only that page. Its children move up one level, preserving their order and remaining descendants. A separate "Close branch" action closes the selected branch and all its descendants.
- Ordinary parent-tab navigation keeps its children attached. A clear change of browsing task is required before the organizer moves the parent and promotes children that still serve the original task.
- When a parent tab changes activity and moves to another task, children that still serve the original task remain there. Promote those children one level in the old location, preserving their order and subtrees. This automatic reclassification moves the repurposed parent, unlike an explicit whole-branch extraction.
- Enable automatic grouping by default from the first AI grouping release. Organize new tabs and refine inactive groups.
- Automatically regroup tabs as task intent becomes clearer, including pulling a distinct task out into its own group.
- In automatic mode, creating a new task preserves candidate order and branches. Place it at the root at the earliest candidate's position, unless its relationship to an existing parent task is clear. In that case, create it as a nested task group under that parent.
- Merge two automatically created groups when they clearly represent the same task and neither has a manual adjustment. If either group has a manual adjustment, propose the merge instead.
- Split an automatically created group when its branches clearly represent separate tasks and it has no manual adjustment. If it has a manual adjustment, propose the split instead.
- Clear task intent can trigger a move immediately, including for the selected tab and other tabs in the current task group. Switching away is not a prerequisite. A move must preserve the selected tab, its page state, and keyboard focus.
- A confident classification into an existing task can take effect without waiting for the connected LLM. The [primary design rule](#principles) and manual adjustments still take precedence over automatic moves.
- Require high confidence for automatic placement. Leave an uncertain tab or branch in Scratchpad rather than making a plausible but incorrect move.
- Without a configured LLM, leave tabs that Jev cannot confidently match to an existing task in Scratchpad until the user manually creates a task.
- When an LLM is configured, send background task-discovery requests for tabs or branches Jev leaves in Scratchpad. Keep them there while requests run; later clear results use the agreed automatic or user-directed task-creation rules.
- When the later excerpt-informed result clearly identifies a better task, correct the initial placement immediately unless the user made a manual adjustment after the initial result.
- Group primarily by inferred task or intent. A group can span websites, and tabs from the same website can belong to different tasks.
- Use separate groups for distinct immediate activities within the same project. For example, "Implement vertical tabs" and "Choose a grouping model" are separate groups, rather than one "Build uw" group.
- Keep brief detours ungrouped until continued browsing makes the separate task intent clear. An unrelated tab does not by itself require a new group.
- A regular tab opened from a shared pin starts ungrouped in the current window. A confident classification may place it immediately; the last active task alone does not determine its placement.
- For now, when a tab clearly belongs to an existing task but has no clear parent page, append it at that task group's top level. Nest it under a page only when the parent relationship is clear, such as a link explicitly opened from that page.
- Organize the regular tab tree independently within each window. Moving regular tabs between windows remains a manual action; [pinned tabs](#pinned-tabs) have their own shared-state requirements.
- Each regular tab has one home position in the hierarchy. Separate tabs showing the same page may belong to different task groups.
- Preserve the user's expanded/collapsed group choices. The organizer does not change them when tasks switch.
- When a selected tab moves into a collapsed task group, keep the group collapsed and show a brief destination cue rather than changing the user's expansion choice.
- Let the user change grouping and ordering. Manual placement takes priority while the tab serves the same browsing task. A later clear change of activity makes the tab eligible for automatic regrouping again; ordinary navigation within the same task preserves the correction.
- For the first release, treat a manual correction as applying only to that tab or branch while its activity remains the same. Do not infer a broader placement rule from one correction.
- Provide a "Reorganize all tabs" button for an additional on-demand AI pass across all tabs in the current window, including the current task group.

### Organization latency

Agreed: the initial whole fast path must complete within **200 ms**, measured from title, URL, and source-tab relationship becoming available to visible placement. This includes preprocessing, queuing, classification, and applying an eligible result to the interface. Apply eligible moves immediately, without adding a delay for the user to finish interacting with the sidebar.

Reference hardware, representative input sizes and task counts, and cold-start expectations still need definition. The latency requirement is not yet benchmarked.

### Scratchpad

Agreed: each window has an always-visible Scratchpad section directly below the pinned-icon row and above task groups. It contains every ungrouped regular tab and candidate branch, rendered as a nested tree. A confident placement moves a whole Scratchpad branch into the matched task by default. Tabs leave it only through confident automatic placement, an accepted task proposal, or a manual move. Scratchpad is not a task group and does not participate in task ordering or automatic task restructuring.

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

Preserve loaded pages, the selected tab, and the moved branches' internal hierarchy. The action takes effect without waiting for an AI decision. Place the new task group at the source task's nesting level, immediately after it.

### Task names

Agreed: once a task is known, give its group a short, action-oriented name describing the immediate activity, such as "Benchmark GLiNER2.5". Revise an automatically generated name only when the task clearly changes. Users can edit names; a user-edited name takes priority over automatic renaming.

### Group order

Agreed: existing sibling task groups keep their relative order when the user switches tasks. Activity or recency does not move a group to the top. Users can drag groups to reorder them; intent-based regrouping can still move tabs or branches when the task becomes clear.

### Organization undo

Agreed: one Undo reverses a complete organization action, including task creation and its associated tab moves. Preserve loaded pages and the current selection. Treat the restored placement as a manual correction while the activity remains the same.

### Organization controls

Agreed: expose organization controls through a custom button menu inside the tab sidebar, so manual organization remains usable while automatic behavior is developed.

Proposed presentation: a compact "Organize" button in the sidebar header. Its menu includes "Move to new task", "Close branch", "Reorganize all tabs", "Undo tab organization", the automatic/user-directed mode choice, and pause controls. Contextual actions remain available on tabs and branches.

Agreed: provide global pause choices for automatic organization: "Pause for 1 hour", "Pause until next launch", and "Pause indefinitely", plus "Resume automatic organization". Pausing stops automatic placements and task proposals across all windows, including application of pending automatic results. Manual tree controls and explicitly requested "Reorganize all tabs" remain available. An explicit reorganization still targets the current window.

The first release supports these three pause choices only. The one-hour pause expires automatically. "Until next launch" resets when uw next starts. "Indefinitely" remains in effect until the user resumes automation.

### Intent context

Agreed: local task inference may use tab titles, URLs, source-tab relationships, and short excerpts of readable page text. Run an initial classification from the title, URL, and source relationship, then re-evaluate when a readable-text excerpt becomes available. Neither pass may block browsing.

Agreed: when the user selects a remote provider for tab organization, requests may include titles, URLs, short relevant page excerpts, current task names, and local classifier findings for the tabs being evaluated.

Agreed: Jev receives no intent context until the user enables Jev tab classification.

Agreed: after opt-in, Jev receives title, URL, and source-tab relationship for the initial pass, then a short readable-text excerpt for the later pass. Show this exact scope during enablement.

## Download name suggestions

Suggest readable filenames using the original name and relevant page context. For example, suggest `electricity-bill-2026-09.pdf` instead of `download.pdf` when the page supports that description.

Agreed naming choices:

- "Always use recommended names": apply recommendations automatically across all sites.
- "Use recommended names for this site": apply recommendations automatically for that site.
- When neither option applies, ask on each download whether to use the recommended name.

Other proposed behavior: allow editing a suggestion or keeping the original name. Preserve the real file extension, sanitize invalid characters, and handle name collisions. Keep the original filename if AI is unavailable or too slow. Naming must not block the download.
