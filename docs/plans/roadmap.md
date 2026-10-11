# Roadmap

The [minimum usable version](../product.md#minimum-usable-version) and [next feature priority](../product.md#automatic-tab-organization) are agreed. Other ordering and completion criteria remain proposed. Technical choices live in [Architecture](../architecture.md). Concrete work tickets are tracked on the [board](#board) below.

## Board

Tickets are concrete pieces of work. Status values: `backlog`, `todo`, `in progress`, `in review`, `done`, and `superseded` for a ticket replaced by a later one. Scores use the [implementability convention](#implementability-estimates) below. Ticket IDs are stable, so commits and patches can reference them.

| ID | Ticket | Status | Score | Notes |
| --- | --- | --- | --- | --- |
| T1 | Blank new tabs open below the current tab | superseded | 8/10 | Now part of T13. |
| T2 | Single-call classification protocol | in review | 7/10 | One OpenAI-compatible chat-completions request per classification returning the placement decision, confidence, and task name, with strict response parsing. Covers the OpenAI-compatible and Ollama connection modes in [automatic-organization.md](automatic-organization.md). |
| T3 | Continuous reclassification coordinator | superseded | 5/10 | Replaced by T15. |
| T4 | Readable-text excerpts in requests | superseded | 6/10 | Carried into T15 per [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md). |
| T5 | Connection setup and Keychain storage | superseded | 7/10 | Replaced by T16. Keychain storage carries over. |
| T6 | Automatic-action feedback and undo | superseded | 6/10 | Replaced by T14 and T15. |
| T7 | Organization pause controls | superseded | 8/10 | Replaced by T15 and T16. |
| T8 | Organize-menu crash fixes | superseded | 7/10 | The crashing adapter is removed in T13. |
| T9 | Organization settings page and uw:// alias | in review | 6/10 | The uw:// alias stands: uw:// URLs alias their chrome:// counterparts, and uw://settings resolves. The settings page is reworked in T16. |
| T10 | Stability safeguards | superseded | 7/10 | Regression tests moved to T13, which also removed the source-text checks. |
| T11 | Decision formats and naming call | in review | 8/10 | Request and response handling for the OpenAI Decisions API and TypeSafe System One (Jev), plus the task naming call, per [automatic-organization.md](automatic-organization.md). Done when `AiProtocolTest.*` passes. |
| T12 | Page tree model | in review | 8/10 | Value type holding tab order, task, and parent link per page, with every tab-tree rule as plain functions, per [native-tab-tree.md](native-tab-tree.md). Done when `PageTreeTest.*` passes. |
| T13 | Window controller on native tab groups | in review | 6/10 | Mirrors the tab strip into the page tree and applies changes back through tab-group and move calls: link parentage, new-tab placement, close promotion, branch commands, undo, session restore. Deletes the old tree model, coordinator, and adapter. Done when the new `TabOrganizationControllerBrowserTest` cases pass. |
| T14 | Sidebar additions | in review | 6/10 | Indentation per nesting level, the Scratchpad label, the Organize button and menu, and the automatic-move message with Undo. Done when the [native tab tree acceptance checks](native-tab-tree.md#acceptance-checks) pass in the built app. All four have landed with browser tests and were checked in headless screenshots. The brief tint on a moved tab is not built. The button and message keep Chromium's stock style until T18. Nobody has clicked through it in a real window yet. |
| T15 | Organizer on native tab groups | in review | 5/10 | Debounced decision requests, naming, staleness checks, branch moves and task creation at confidence 0.90 or more, one undo per action, and pause. Done when the [automatic organization acceptance checks](automatic-organization.md#acceptance-checks) pass against a test server. `AutomaticOrganizerBrowserTest.*` passes against a local test server for all three formats. No live provider has been called. On 2026-10-11 one run in about twelve timed out waiting for the first request in `DecisionModelFilesALinkedTabIntoItsTask`. It did not happen again and the cause is unknown. The test now reports what the server received when it times out. |
| T16 | Settings: decision model, language model, pause | in review | 7/10 | Settings page with both connections and the pause controls. Validation gates enablement, a failed save turns automatic organization off, and the model list is fetched with GET. Done when each provider validates from the page against a test server. `UwOrganizationPageBrowserTest.*` drives the real page and passes for Jev, the OpenAI Decisions API, a compatible endpoint, and a language model alone. The provider choices are dropdowns; the segmented control in the design waits for T18. No live provider has been called. |
| T17 | Tooling and docs cleanup | in review | 9/10 | Fold `verify` into the main CLI, make the macOS workflow manual until a runner exists, and give build status one home. `verify` is a CLI command with a tooling test, the workflow runs only by hand, and build status lives in the README's verification section. A full `pnpm run verify` passed on 2026-10-11. |
| T18 | Arc-style look | in progress | 4/10 | Tinted window, page in a rounded card, favorites tiles, and folder-style task headers, per the [plan](arc-style-look.md) approved on 2026-10-11. The screenshot helper, the default tints in light and dark mode, the tab row sizes, and the favorites tiles are built and checked in headless screenshots. The page card, task folder rows, toolbar height, and the user's own tint are not. |

## 1. Choose the browser foundation

The [upstream Chromium baseline](../../README.md#upstream-baseline) built and passed headless smoke testing on Apple Silicon. The [customized build](../../README.md#customizing-chromium) passes its headless smoke test; native UI checks are pending.

Validate the source-customization approach for the [initial platform](../product.md#minimum-usable-version). Establish the security-update path with a patch-update trial and measure a performance baseline. Check compatibility with the [required extensions](../product.md#required-extensions), ad blocking, download hooks, and session persistence.

Done when the [architecture](../architecture.md#code-organization) has measured evidence that the approach supports the headline features and manageable Chromium updates.

## 2. Make the browser usable daily

Deliver the minimum usable version, including the [automatic organization MVP](automatic-organization.md). Identify the user's required everyday browser features before defining its acceptance checks.

Done when the user can complete their daily workflows in uw without blocking crashes or workflow regressions, and automatic organization files tab branches reliably without fighting manual placement. Verify the [required extensions](../product.md#required-extensions), session restoration, and responsiveness during normal use.

## 3. Add task-based tab organization

Start with the [native tab tree](native-tab-tree.md) and [automatic organization](automatic-organization.md) plans; both are part of the minimum usable version. Shared live pins follow a measured prototype and need separate implementation approval. The agreed behavior is in [Product](../product.md#automatic-tab-organization).

Done when the tab tree supports task regrouping and extraction into separate groups, manual adjustments affect future organization, and users can configure the agreed AI connection modes. Verify automatic and user-directed organization, session restoration, undo, page continuity during moves, and that local inference keeps page context on the device. Measure the [organization latency requirement](../product.md#organization-latency) and verify immediate application without a sidebar-interaction delay. Check that inference completion does not block browsing, take focus, or override intervening manual actions, following the [primary design rule](../product.md#principles).

Clutter removal can follow tab organization. Its order relative to the other later additions remains open.

## 4. Add download naming and page watches

Deliver [download naming](../product.md#download-name-suggestions) and [page watches](../product.md#scheduled-page-watches).

Done when naming follows the user's preferences and downloads still work when inference fails. Verify session reuse, the sign-in-required status, scheduled checks, the selected reporting mode, visible failures, and the [agreed app availability and catch-up behavior](../product.md#scheduled-page-watches).

## 5. Add prompt-built extensions

Complete one coding-tool integration before adding the others. Deliver the prompt-to-extension flow, including trial runs and rollback.

Done when a user can generate a page modification, inspect its site access, enable it, refine it, and restore a prior version. Measure browsing performance with generated extensions active.

## Future: delegated browser tasks

[Agent-controlled browser tasks](../product.md#delegated-browser-tasks) are agreed future direction. Define their product behavior before scheduling implementation.

## Implementability estimates

10 means straightforward to build reliably; 1 means very difficult or uncertain. These are rough estimates, not progress scores. They assume a Chromium base and an initial macOS release, and include integration, accuracy, and ongoing maintenance. Revisit them as scope and technical choices become clearer.

### Current features

Feature scope and agreed behavior are recorded in [Product](../product.md).

| Feature | Score | Main consideration |
| --- | --- | --- |
| [Chromium build foundation](../../README.md#building) | 7/10 | Verified on one Apple Silicon Mac; Xcode compatibility and ongoing Chromium updates still need maintenance. |
| [Minimal uw branding and vertical-tab default](../../README.md#customizing-chromium) | 9/10 | Small identity and preference patches reuse upstream native tabs. The customized build and headless smoke test pass; native UI behavior remains unverified. |
| [Chrome-like browsing with vertical tabs](../product.md#minimum-usable-version) | 7/10 | Keeping normal browser behavior intact matters more than drawing the sidebar. |
| [Native tree-style tabs](../product.md#automatic-tab-organization) | 7/10 | Tasks are native tab groups. uw adds page parents, indentation, child promotion on close, and restore of parent links. |
| [Arc-style look](arc-style-look.md) | 4/10 | Chromium already has settings for the sizes, colors, and rounded page corners. The tint gradient, the user's own tint, and the task folder row need custom drawing, and each Chromium update can move the views it touches. |
| [Pinned icons and home URLs](../product.md#pinned-tabs) | 8/10 | Shared saved entries and sidebar icons are bounded work. Cmd-W is a no-op; reset and explicit home-URL editing behavior are defined. |
| [Pinned-tab Peek previews](../product.md#pinned-tabs) | 6/10 | Same-site navigation remains in the pin; off-site links open a temporary preview that can promote into Scratchpad. |
| [Shared live pinned-page state](../product.md#pinned-tabs) | 3/10 | The live page transfers between windows and remains alive without a browser window. Quit uses a new-tab restore offer; Chromium ownership and lifecycle need a trial. |
| [Required Chrome-extension compatibility](../product.md#required-extensions) | 7/10 | Browser integration and interactions between the named extensions need validation on macOS. |
| [Explicit AI setup](../product.md#ai-connections) | 9/10 | A bounded connection-selection flow. |
| [Managed local models](../product.md#ai-connections) | 6/10 | Device compatibility, download management, and resource use. |
| [Ollama connection](../product.md#ai-connections) | 9/10 | An established API with a small configuration flow. |
| [Online provider connections](../product.md#ai-connections) | 8/10 | Several API formats and model capabilities to support. |
| [Decision model connection](../architecture.md#tab-organization-models) | 7/10 | Jev, the OpenAI Decisions API, or a compatible endpoint decides placement. Request shapes come from public documentation; no provider has been called live yet. |
| [On-demand AI tab reorganization](../product.md#automatic-tab-organization) | 6/10 | Now needs tree-aware changes and restoration while preserving manual choices within the current window. |
| [Automatic task-based tree organization](../product.md#automatic-tab-organization) | 5/10 | The initial title/URL/source fast path must meet the agreed latency budget, then a clearer excerpt-informed pass may correct it. Background LLM discovery handles unmatched Scratchpad branches, with stale-result cancellation. |
| [Automatic organization](automatic-organization.md) | 6/10 | Decisions from the decision model or the language model, names from the language model, stale-result and manual-override protection, page excerpts, Keychain secrets, undo, pause, and sidebar feedback. |
| [Scratchpad](../product.md#scratchpad) | 8/10 | A window-local nested tree below pins and above task groups for ungrouped tabs and pending candidates. Confident placement moves a whole branch. |
| [User-directed task proposals](../product.md#user-directed-mode) | 6/10 | Pending proposals remain inline without moving tabs. Accepted proposals create a root group at the earliest candidate; rejected proposals stay suppressed until activity changes. |
| [Manual task extraction](../product.md#manual-task-extraction) | 8/10 | Once the tab tree exists, this deterministically moves selected branches into a sibling task group without waiting for inference. |
| [Task names](../product.md#task-names) | 8/10 | Once the task is known, produce a short activity name. Keep generated names stable until the task changes and preserve user-edited names. |
| [Stable group order](../product.md#group-order) | 8/10 | Preserve sibling order through task switches and allow deliberate manual reordering. |
| [Matched-tab placement](../product.md#automatic-tab-organization) | 8/10 | Append a tab at a matched task's root when it has no clear parent page, preserving the existing tree. |
| [Organization undo](../product.md#organization-undo) | 7/10 | Reverse related tree changes as one operation while preserving page state, selection, and manual-correction behavior. |
| [Sidebar organization menu](../product.md#organization-controls) | 8/10 | Expose organization commands and global timed, next-launch, and indefinite pause controls through native button/menu controls. |
| [Learning from manual adjustments](../product.md#automatic-tab-organization) | 8/10 | The first release preserves each corrected tab or branch while its activity stays the same. Broader learning is deferred. |
| [Scheduled page watches with catch-up](../product.md#scheduled-page-watches) | 6/10 | Signed-in session reuse and dependable change detection across changing websites. |
| [Watch reporting and notifications](../product.md#scheduled-page-watches) | 8/10 | Native notifications, history, and per-watch mute use established patterns once results are dependable. |
| [Page prompts and element selection](../product.md#prompt-built-extensions) | 8/10 | Selecting and describing the intended page content. |
| [Prompt-built extensions and site customization](../product.md#prompt-built-extensions) | 5/10 | Generating working changes across different sites and coding tools. |
| [Customization persistence and controls](../product.md#prompt-built-extensions) | 8/10 | Remember enabled changes across visits and expose per-site editing and removal. |
| [Open-ended agent automation](../product.md#delegated-browser-tasks) | 3/10 | Arbitrary multi-step tasks are hard to complete and verify consistently. |
| [Recommended filenames and naming preferences](../product.md#download-name-suggestions) | 8/10 | Bounded settings; useful names depend on accurate context. |
| [Clutter-removal options page](../product.md#ad-blocking-and-privacy) | 9/10 | Clear, independent settings; removal behaviors scored separately. |
| [Ad blocking](../product.md#ad-blocking-and-privacy) | 8/10 | Established filter lists, with site compatibility work. |
| [Apply cookie preferences](../product.md#ad-blocking-and-privacy) | 6/10 | Consent interfaces and available choices vary across sites. |
| [Social button removal](../product.md#ad-blocking-and-privacy) | 8/10 | Common patterns, with site-specific exceptions. |
| [Browser privacy controls](../product.md#ad-blocking-and-privacy) | 8/10 | Established browser behavior must extend consistently to AI features. |
| Tracker blocking | 8/10 | Established filters, with ongoing compatibility maintenance. |
| Newsletter-popup dismissal | 7/10 | Many layouts and interaction patterns. |
| Autoplay blocking | 8/10 | Established browser controls, with site exceptions. |
| Hiding chat widgets and sticky promotions | 7/10 | Identifying unwanted content without removing useful controls. |
| Suppressing site notification requests | 9/10 | A bounded browser permission preference. |
| Per-site cleanup overrides | 9/10 | Mostly settings and clear precedence rules. |
