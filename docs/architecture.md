# Architecture

The fixed direction and current implementation are distinguished from proposed feature organization below.

## Fixed direction

Chromium is the browser engine. The product needs control over the browser interface, tabs, downloads, privacy, and automation.

The current implementation customizes Chromium source through a small, reviewed patch set. It uses Chromium's browser interface and native Views tabs. Validate extension support, filtering hooks, resource use, and the security-update workload before expanding the changes.

## Build foundation

The initial upstream build passed on Apple Silicon. The wrapper now applies uw customizations before compiling `uw.app` with Chromium's build tools. Commands, current scope, and verification status live in the [root README](../README.md#customizing-chromium).

## Code organization

Use one product repository and an ignored Chromium checkout. Keep Chromium integration edits in `chromium/patches/`, with pins and GN arguments alongside them. Product C++ lives in tracked `browser/` and `components/` directories; preparation links those into `src/uw/` after applying the patch series. `scripts/` owns TypeScript build orchestration; `tests/` tests that tooling. The [preparation workflow](../README.md#customizing-chromium) defines how the checkout is assembled.

This combines Brave's use of ordinary product source with Helium's explicit patch ordering. [The source comparison](chromium-organization-research.md#main-comparison) records their actual mechanisms and maintenance costs. uw currently needs only the ordered patches.

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

## Native tab tree

The user has approved the [manual native tab-tree slice](plans/native-tab-tree.md). The existing patch only enables Chromium's native vertical-tab default; custom nesting and intent-based regrouping are not implemented yet. The [pinned-source investigation](chromium-tab-api-research.md) identifies reusable controls and the required layout/input adaptations.

[Horse's documented hierarchy](horse-tab-organization-research.md) includes parent pages and non-page containers. Its organizing behavior does not establish a compatible implementation for uw. Trial the [agreed tree structure](product.md#automatic-tab-organization) against Chromium tabs, native Views, and session restoration. Check keyboard navigation, drag-and-drop, parent-tab lifecycle, and page continuity when branches move.

Use the ordinary feature-source layout above for substantial hierarchy logic. Keep tree state separate from its view so regrouping can be tested without driving the interface. Updated [implementability estimates](plans/roadmap.md#implementability-estimates) include this additional work.

The [organization controls](product.md#organization-controls) use a custom button menu in the tab sidebar. Proposed: implement it with native Views menu controls and route it and tab context menus through the same organization module. Chromium's existing [Tab menu](https://github.com/chromium/chromium/blob/792bf6722e73a45aa9e47c163b9901bdc17f3230/chrome/browser/ui/cocoa/main_menu_builder.mm) provides a reference for browser-command routing. Keep organization undo distinct from the page's ordinary text-editing undo.

The organization pause is global. It supports one hour, until next launch, and indefinite durations. Each window's automatic organization must honor the same pause state, while its regular tab tree and explicitly requested operations remain window-scoped.

## Shared pinned tabs

The [pinned-tab requirements](product.md#pinned-tabs) include a continuing live page session across windows. Sharing icon order and saved URLs alone does not satisfy that requirement. Arc's [documentation](arc-pinned-tabs-research.md) does not establish the Chromium ownership or presentation mechanism to reuse.

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

Run model inference and background page checks outside the UI process. Limit concurrency, prioritize foreground browsing, and release idle model resources. Do not keep every watched page open between runs.

Establish a stock Chromium baseline on the same hardware and test pages. Measure startup, tab switching, active and suspended-tab memory, idle CPU, and energy use. Repeat with local inference and page watches running.

Tab classification has an [agreed latency budget](product.md#organization-latency). Define its measurement conditions and benchmark the actual pipeline. Set other performance budgets after the first baseline, and record test conditions.

## Tab organization models

Agreed role: tab classification calls a dedicated fast decision model, alongside an LLM connected through the [supported AI connection modes](product.md#ai-connections). The current choice is [TypeSafe Jev](https://typesafe.ai/), which its developer describes as an early-access model for typed decisions with calibrated confidence. GLiNER2.5 is no longer the selected fast-path candidate.

Jev's published claims do not establish that it can classify browsing tasks accurately or meet uw's [context-to-placement deadline](product.md#organization-latency). Its execution location, endpoint behavior, input limits, terms, and local-runtime availability need validation before implementation.

Proposed integration:

- After explicit opt-in, send Jev the title, URL, and source relationship for initial classification. Re-evaluate with a short readable-text excerpt when it becomes available, using the [agreed intent context](product.md#intent-context) to score matches to existing task descriptions.
- Use the connected LLM to discover new tasks, refine task descriptions when intent is ambiguous, and propose larger organization changes using the [agreed intent context](product.md#intent-context). Classification against task descriptions stays with the fast model.
- Without a configured LLM, Jev's no-match or low-confidence result leaves the tab in Scratchpad. The user can still create a task manually.
- With a configured LLM, send background discovery for Scratchpad branches that Jev cannot match. Cancel or discard requests when their branch changes, closes, or receives a manual adjustment. Apply only results that still match current tree state.
- Apply validated changes through native browser code, honoring the [agreed organization rules](product.md#automatic-tab-organization).

The [confident-placement rule](product.md#automatic-tab-organization) allows classification results to take effect without an LLM response. A clearer excerpt-informed result corrects the initial placement unless the user acted in between. Confidence calibration and failure behavior still need decisions and measured trials. Apply model results asynchronously and check them against current tab state so inference completion does not override an intervening manual action or newer navigation.

Verify Jev's access model, endpoint contract, input limits, and terms before implementation. Measure end-to-end latency and intent accuracy on the target Mac; model-level speed claims alone are insufficient. If Jev is remote, include network time in the [agreed latency budget](product.md#organization-latency). The [required explicit opt-in](product.md#ai-connections) precedes any intent-context request. The integration is unimplemented. Implementability: **5/10**, including provider dependency, latency, and intent quality, pending the trial.

## Data and execution

Keep browser state and automation history local by default. Send only the context needed for the configured remote operation.

Treat page content as input, not authority to change browser permissions or execute commands. Run generated extensions with declared site access and Chromium's permission boundaries. Keep the browser sandbox enabled.

Ad blocking needs integration-level evaluation. Do not assume a particular extension API can provide the required filtering behavior.

## Open decisions

| Decision | What must be resolved |
| --- | --- |
| Chromium updates | Patch repair cost across a version bump, extension compatibility, and time to ship security updates. |
| Platform support | Hardware requirements and packaging for the [initial platform](product.md#minimum-usable-version), plus support for later platforms. |
| Managed local AI | Runtime, model licenses, hardware fit, download source, integrity checks, and model updates. |
| Tab organization models | Validate Jev's provider contract and data handling, then measure its task accuracy, confidence calibration, and context-to-placement latency. |
| Tab hierarchy | Mapping to Chromium's tab model, stable identities during branch changes, persistence, and live-move feedback. |
| Shared pins | Shared live-page presentation and reset, unpin, and window-shutdown behavior. |
| Background scheduling | Scheduler integration with the [agreed app lifecycle and catch-up behavior](product.md#scheduled-page-watches), plus daylight-saving handling. |
| Watch sessions | How checks reuse the user's existing browser session while following the [agreed sign-in behavior](product.md#scheduled-page-watches). |
| Coding-tool connections | Supported integration methods, credentials, and how tools return extension artifacts. |
| Performance budgets | Validate the agreed context-to-placement deadline on reference hardware, define representative input sizes and task counts, and settle cold-start expectations and acceptable overhead versus stock Chromium. |

Resolve these through small, measured trials before expanding the implementation.
