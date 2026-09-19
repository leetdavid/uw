# Architecture

Technical details below are proposals except for the fixed direction.

## Fixed direction

Chromium is the browser engine. The product needs control over the browser interface, tabs, downloads, privacy, and automation.

The integration approach is undecided. Compare a Chromium fork with an embedding approach before choosing a UI framework or committing to a large implementation. Evaluate extension support, built-in filtering, browser APIs, resource use, and the work needed to ship Chromium security updates.

## Build foundation

The initial implementation compiles pinned upstream Chromium source into `Chromium.app` using Chromium's build tools. Establish a working source build before changing browser behavior. Commands and verification status live in the [root README](../README.md#building).

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
| Chromium integration | Fork or embedding approach, extension compatibility, maintenance cost, and update path. |
| Platform support | Hardware requirements and packaging for the [initial platform](product.md#minimum-usable-version), plus support for later platforms. |
| Managed local AI | Runtime, model licenses, hardware fit, download source, integrity checks, and model updates. |
| Background scheduling | Scheduler integration with the [agreed app lifecycle and catch-up behavior](product.md#scheduled-page-watches), plus daylight-saving handling. |
| Watch sessions | How checks reuse the user's existing browser session while following the [agreed sign-in behavior](product.md#scheduled-page-watches). |
| Coding-tool connections | Supported integration methods, credentials, and how tools return extension artifacts. |
| Performance budgets | Reference hardware, representative workloads, and acceptable overhead versus stock Chromium. |

Resolve these through small, measured trials before expanding the implementation.
