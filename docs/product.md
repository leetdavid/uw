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

- Browsing comes first. AI work must not stall navigation, typing, or tab switching.
- Keep the user in control. Automatic changes should be understandable and reversible.
- Measure performance against a [repeatable baseline](architecture.md#performance).

## Browser experience

Agreed: use [automatic tab organization](#automatic-tab-organization) to help track the current task and its tabs. This replaces the earlier Arc-style workspace proposal, which did not solve the user's task-tracking problem.

Proposed: suspend inactive tabs when appropriate, restore sessions reliably, and provide keyboard shortcuts for common browsing actions.

## AI connections

Agreed setup: when the user first enables an AI feature, ask them to choose a connection mode. Ordinary browsing works without AI setup.

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

Agreed:

- Enable automatic grouping by default from the first AI grouping release. Organize new tabs and refine inactive groups.
- Group primarily by inferred task or intent. A group can span websites, and tabs from the same website can belong to different tasks.
- Keep existing tabs in the current task group in place during automatic updates.
- Let the user change grouping and ordering. Manual moves take priority over background organization.
- Learn from these manual adjustments to improve future organization.
- Provide a "Reorganize all tabs" button for an additional on-demand AI pass across all tabs, including the current task group.

Sorting rules and how adjustments carry forward remain open.

Proposed: preserve pinned tabs and make regrouping undoable.

## Download name suggestions

Suggest readable filenames using the original name and relevant page context. For example, suggest `electricity-bill-2026-09.pdf` instead of `download.pdf` when the page supports that description.

Agreed naming choices:

- "Always use recommended names": apply recommendations automatically across all sites.
- "Use recommended names for this site": apply recommendations automatically for that site.
- When neither option applies, ask on each download whether to use the recommended name.

Other proposed behavior: allow editing a suggestion or keeping the original name. Preserve the real file extension, sanitize invalid characters, and handle name collisions. Keep the original filename if AI is unavailable or too slow. Naming must not block the download.
