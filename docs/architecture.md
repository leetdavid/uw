# Architecture

The fixed direction and current implementation are distinguished from proposed feature organization below.

## Fixed direction

Chromium is the browser engine. The product needs control over the browser interface, tabs, downloads, privacy, and automation.

The current implementation customizes Chromium source through a small, reviewed patch set. It uses Chromium's browser interface and native Views tabs. Validate extension support, filtering hooks, resource use, and the security-update workload before expanding the changes.

## Build foundation

The initial upstream build passed on Apple Silicon. The wrapper now applies uw customizations before compiling `uw.app` with Chromium's build tools. Commands, current scope, and verification status live in the [root README](../README.md#customizing-chromium).

## Code organization

Use one product repository and an ignored Chromium checkout. Keep the first customizations in `chromium/patches/`, with pins and GN arguments alongside them. `scripts/` owns TypeScript build orchestration; `tests/` tests that tooling. The [preparation workflow](../README.md#customizing-chromium) defines how the checkout is assembled.

This combines Brave's use of ordinary product source with Helium's explicit patch ordering. [The source comparison](chromium-organization-research.md#main-comparison) records their actual mechanisms and maintenance costs. uw currently needs only the ordered patches.

As substantial product code arrives, add directories for real implementations:

| Proposed directory | What belongs there |
| --- | --- |
| `browser/<feature>/` | Profile lifetime, preference registration, and integration with browser tabs, downloads, or windows. Native Views code belongs under `browser/ui/views/<feature>/`. |
| `components/<feature>/` | Feature logic behind a small interface, with its own GN target and colocated tests. Use Chromium types where useful; avoid dependencies on `//chrome/browser`. |
| `resources/<feature>/` | Product-owned WebUI TypeScript, HTML, and styles when a feature needs WebUI. |

Keep substantial new C++ in ordinary files. A future preparation step can expose those files at Chromium's `src/uw`, with narrow GN patches wiring explicit targets into the browser. That source mapping is not implemented yet. Add it with the first module that needs it.

Organize by feature within those directories. For example, page-watch scheduling, comparison, and history should sit behind one module interface; browser integration supplies the profile and page access. Put tests beside that implementation. Keep `//chrome/browser` dependencies in the browser integration code so feature targets can be tested independently.

Avoid whole-file Chromium replacements, macro-based source interception, and a general rewrite engine until a concrete integration requires them. Keep platform conditions and packaging in this repository while macOS is the only target.

Implementability: **7/10** for this organization including ongoing Chromium updates, using the [roadmap convention](plans/roadmap.md#implementability-estimates). A successful version-bump trial is still needed to measure maintenance cost.

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

Set numeric budgets after the first baseline. Record test conditions so "performant" has a reproducible meaning.

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
| Background scheduling | Scheduler integration with the [agreed app lifecycle and catch-up behavior](product.md#scheduled-page-watches), plus daylight-saving handling. |
| Watch sessions | How checks reuse the user's existing browser session while following the [agreed sign-in behavior](product.md#scheduled-page-watches). |
| Coding-tool connections | Supported integration methods, credentials, and how tools return extension artifacts. |
| Performance budgets | Reference hardware, representative workloads, and acceptable overhead versus stock Chromium. |

Resolve these through small, measured trials before expanding the implementation.
