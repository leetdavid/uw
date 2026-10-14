# Architecture

The fixed direction and current implementation are distinguished from proposed feature organization below.

## Fixed direction

Chromium is the browser engine. The product needs control over the browser interface, tabs, downloads, privacy, and automation.

The current implementation customizes Chromium source through a small, reviewed patch set. It uses Chromium's browser interface and native Views tabs. Validate extension support, filtering hooks, resource use, and the security-update workload before expanding the changes.

## Build foundation

The initial upstream build passed on Apple Silicon. The wrapper now applies uw customizations before compiling `uw.app` with Chromium's build tools. Commands, current scope, and verification status live in the [root README](../README.md#customizing-chromium).

## Code organization

Use one product repository and an ignored Chromium checkout. Keep Chromium integration edits in `chromium/patches/`, with pins and GN arguments alongside them. Product C++ lives in tracked `browser/` and `components/` directories; preparation links those into `src/uw/` after applying the patch series. `scripts/` owns TypeScript build orchestration; `tests/` tests that tooling. The [preparation workflow](../README.md#customizing-chromium) defines how the checkout is assembled.

This combines Brave's use of ordinary product source with Helium's explicit patch ordering. [The source comparison](research/chromium-organization-research.md#main-comparison) records their actual mechanisms and maintenance costs.

As substantial product code arrives, add directories for real implementations:

| Proposed directory | What belongs there |
| --- | --- |
| `browser/<feature>/` | Profile lifetime, preference registration, and integration with browser tabs, downloads, or windows. Native Views code belongs under `browser/ui/views/<feature>/`. |
| `components/<feature>/` | Feature logic behind a small interface, with its own GN target and colocated tests. Use Chromium types where useful; avoid dependencies on `//chrome/browser`. |
| `resources/<feature>/` | Product-owned WebUI TypeScript, HTML, and styles when a feature needs WebUI. |

Keep substantial new C++ in ordinary files. Preparation exposes it at Chromium's `src/uw`, with narrow GN patches wiring explicit targets into the browser. The [manual tab-tree slice](plans/native-tab-tree.md) is the first consumer of this mapping. The links are a build-time view, not a second source of truth.

Organize by feature within those directories. For example, page-watch scheduling, comparison, and history should sit behind one module interface; browser integration supplies the profile and page access. Put tests beside that implementation. Keep `//chrome/browser` dependencies in the browser integration code so feature targets can be tested independently.

Avoid whole-file Chromium replacements, macro-based source interception, and a general rewrite engine until a concrete integration requires them. Keep platform conditions and packaging in this repository while macOS is the only target.

Implementability: **7/10** for this organization including ongoing Chromium updates, using the [roadmap convention](plans/roadmap.md#implementability-estimates). A successful version-bump trial is still needed to measure maintenance cost.

### Native lifetime rules

These rules come from fixed native crashes. They apply to every uw customization:

- Never change `TabStripModel` from inside its observer notifications. The model holds a reentrancy guard while notifying, so post a task and move tabs there.
- Never add a view to a Chromium tab container and remove it later. The animating layout manager caches raw pointers to laid-out children. A uw view inside one of those containers is created with its host and dies with it; show and hide it instead.
- Never keep an unowned pointer to an object owned by a dialog, bubble, or host view. Look it up from the owning model while that model is alive.

The build turns Chromium's `DCHECK` and dangling-pointer checks off, as official Chrome builds do. With them on, real sites crashed tabs on checks in Chromium's own code: nine crashes in two days of use, none in uw's code. chatgpt.com was one of them. This reverses the earlier rule, decided on 2026-10-13.

`CHECK` stays on. Treat a crash on one as the safeguard working: fix the lifetime instead of silencing the check. The tests no longer catch what the two disabled checks caught, so the rules above matter more.

## Native tab tree

A task is a native Chromium tab group. uw adds page tabs nested under their parent page, the Scratchpad label, the Organize button, and the organizer. See [ADR 0002](adr/0002-tasks-are-native-tab-groups.md) and the [approved plan](plans/native-tab-tree.md). The [pinned-source investigation](research/chromium-tab-api-research.md) identifies the Chromium controls this reuses.

Keep the rules in `components/tab_organization/page_tree`, separate from views, so they are tested without driving the interface. The window-owned controller mirrors `TabStripModel` into a page tree and applies changes back through Chromium's tab-group and move calls.

The [organization controls](product.md#organization-controls) use a button menu in the tab sidebar. Route it and the tab context menu through the same controller. Keep organization undo distinct from the page's ordinary text-editing undo.

The organization pause is global. It supports one hour, until next launch, and indefinite durations. Each window's automatic organization must honor the same pause state, while manual commands remain window-scoped.

## Terminal

A [terminal tab](CONTEXT.md#language) is a `uw://terminal` page drawn by ghostty-web. A shell host in the browser process runs the shell, and every byte to the shell passes through one function there. See [ADR 0004](adr/0004-the-terminal-draws-with-ghostty-web.md) and the [plan](plans/terminal-tab.md). The shell host and the page are built. The plan's later stages wait for approval.

## Shared pinned tabs

The [pinned-tab requirements](product.md#pinned-tabs) include a continuing live page session across windows. Sharing icon order and saved URLs alone does not satisfy that requirement. Arc's [documentation](research/arc-pinned-tabs-research.md) does not establish the Chromium ownership or presentation mechanism to reuse.

Implement the [confirmed pin interactions](product.md#pinned-tabs). A live pin transfers to the window that selects it; the previous window renders an inactive pinned view. Closing its owning window detaches the live session without discarding it. Sessions remain alive when the final window closes while uw runs. Navigation within the pin's scheme and registrable domain stays in the pin; off-site links use temporary Peek previews. Home replacement and manual home editing have different navigation behavior. Unpinning removes the shared pin and makes the live page a regular tab in its current window. After quit, launch a new tab and offer restoration rather than automatically restoring the session. Accepting restoration recovers pins with the regular session; dismissing it leaves the pins available for normal opening. Trial transfer, detachment, retention without a window, focus, navigation, previews, in-progress forms, media, resetting, home edits, unpinning, and restoration in Chromium. The prototype must show continuity when a pin transfers between windows, its owning window closes, and a new window opens after all others close. The existing window-local tab-tree proposal does not establish those capabilities.

## Proposed responsibilities

These describe responsibilities, not a required package or service layout.

| Area | Responsibility |
| --- | --- |
| Chromium integration | Rendering, navigation, browser processes, sandboxing, networking, downloads, and extension execution. |
| Browser interface | Browsing controls, vertical tabs, tab groups, and session restoration. |
| AI execution | Managed model downloads and inference, Ollama, remote provider adapters, capability checks, cancellation, and resource limits. |
| Automation | Watch schedules, page capture, comparisons, result history, and notifications. |
| Extension generation | Coding-tool connections, generated source, trial runs, installation, and version rollback. |
| Local state | Browser settings, watch definitions, extension records, and user preferences. Secrets belong in the OS credential store. |

## Performance

Run model inference and background page checks outside the UI process. Limit the concurrency of background work, prioritize foreground browsing, and release idle model resources. Chats the user starts are their own requests and have no cap; see [agent chats](plans/agent-chats.md#agreed-scope). Do not keep every watched page open between runs.

Establish a stock Chromium baseline on the same hardware and test pages. Measure startup, tab switching, active and suspended-tab memory, idle CPU, and energy use. Repeat with local inference and page watches running.

Tab classification has an [agreed latency budget](product.md#organization-latency). Define its measurement conditions and benchmark the actual pipeline. Set other performance budgets after the first baseline, and record test conditions.

## Tab organization models

Settings hold an optional decision model and a language model. See [ADR 0003](adr/0003-decision-model-beside-the-language-model.md) and the [approved plan](plans/automatic-organization.md).

| Connection | Providers | Wire format |
| --- | --- | --- |
| Decision model | Jev (TypeSafe) | `POST <base>/systemone` |
| Decision model | OpenAI Decisions API, or a compatible endpoint | `POST <base>/decisions` |
| Language model | Ollama, or an OpenAI-compatible endpoint | `POST <base>/chat/completions` |

The decision model picks one option: an existing task, a new task, or the Scratchpad. The language model names a new task. With no decision model, the language model does both in one call.

Apply results asynchronously and check them against current tab state, so a late response never overrides a manual action or a newer navigation. A result needs the [agreed confidence](plans/automatic-organization.md#agreed-scope) to move anything.

The request and response shapes were taken from the providers' public documentation on 2026-10-11. On 2026-10-12 TypeSafe's live API answered uw's System One validation request in that shape, and listed its models as `models[].name`. The OpenAI Decisions API has not been called. Measure accuracy, confidence calibration, and latency on the target Mac before relying on any provider's published claims. The [200 ms budget](product.md#organization-latency) remains a future milestone.

## Data and execution

Keep browser state and automation history local by default. Send only the context needed for the configured remote operation.

Treat page content as input, not authority to change browser permissions or execute commands. Run generated extensions with declared site access and Chromium's permission boundaries. Keep the browser sandbox enabled.

A terminal tab takes text only from the user. Commands from any other source are a [later stage](plans/terminal-tab.md#later-stages) and need the user's approval, so page content carries no authority there either.

Ad blocking needs integration-level evaluation. Do not assume a particular extension API can provide the required filtering behavior.

## Open decisions

| Decision | What must be resolved |
| --- | --- |
| Chromium updates | Patch repair cost across a version bump, extension compatibility, and time to ship security updates. |
| Platform support | Hardware requirements and packaging for the [initial platform](product.md#minimum-usable-version), plus support for later platforms. |
| Managed local AI | Runtime, model licenses, hardware fit, download source, integrity checks, and model updates. |
| Tab organization models | Call each provider live, check its data handling, then measure task accuracy, confidence calibration, and context-to-placement latency. |
| Tab hierarchy | Keeping page nesting consistent with native drags, stable identities during branch moves, and persistence. |
| Shared pins | Shared live-page presentation and reset, unpin, and window-shutdown behavior. |
| Background scheduling | Scheduler integration with the [agreed app lifecycle and catch-up behavior](product.md#scheduled-chats), plus daylight-saving handling. |
| Watch sessions | How checks reuse the user's existing browser session while following the [agreed sign-in behavior](product.md#scheduled-chats). |
| Coding-tool connections | Supported integration methods, credentials, and how tools return extension artifacts. |
| Performance budgets | Validate the agreed context-to-placement deadline on reference hardware, define representative input sizes and task counts, and settle cold-start expectations and acceptable overhead versus stock Chromium. |

Resolve these through small, measured trials before expanding the implementation.
