# Roadmap

The [minimum usable version](../product.md#minimum-usable-version) and [next feature priority](../product.md#automatic-tab-organization) are agreed. Other ordering and completion criteria remain proposed. Technical choices live in [Architecture](../architecture.md).

## 1. Choose the browser foundation

The [upstream Chromium baseline](../../README.md#upstream-baseline) built and passed headless smoke testing on Apple Silicon. The wrapper now prepares the [first uw customizations](../../README.md#customizing-chromium); their full build and UI checks are pending.

Validate the source-customization approach for the [initial platform](../product.md#minimum-usable-version). Establish the security-update path with a patch-update trial and measure a performance baseline. Check compatibility with the [required extensions](../product.md#required-extensions), ad blocking, download hooks, and session persistence.

Done when the [architecture](../architecture.md#code-organization) has measured evidence that the approach supports the headline features and manageable Chromium updates.

## 2. Make the browser usable daily

Deliver the minimum usable version. Identify the user's required everyday browser features before defining its acceptance checks.

Done when the user can complete their daily workflows in uw without blocking crashes or workflow regressions. Verify the [required extensions](../product.md#required-extensions), session restoration, and responsiveness during normal use.

## 3. Add task-based tab organization

Deliver [tab organization](../product.md#automatic-tab-organization) with the [AI setup](../product.md#ai-connections) needed to use it. Resolve sorting rules and learning behavior first.

Done when tabs group by task, manual adjustments affect future organization, and users can configure the agreed AI connection modes. Verify undo, browsing responsiveness, and that local inference keeps page context on the device.

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
| [Minimal uw branding and vertical-tab default](../../README.md#customizing-chromium) | 9/10 | Small identity and preference patches reuse upstream native tabs. Source-level checks pass; the customized build and UI remain unverified. |
| [Chrome-like browsing with vertical tabs](../product.md#minimum-usable-version) | 7/10 | Keeping normal browser behavior intact matters more than drawing the sidebar. |
| [Required Chrome-extension compatibility](../product.md#required-extensions) | 7/10 | Browser integration and interactions between the named extensions need validation on macOS. |
| [Explicit AI setup](../product.md#ai-connections) | 9/10 | A bounded connection-selection flow. |
| [Managed local models](../product.md#ai-connections) | 6/10 | Device compatibility, download management, and resource use. |
| [Ollama connection](../product.md#ai-connections) | 9/10 | An established API with a small configuration flow. |
| [Online provider connections](../product.md#ai-connections) | 8/10 | Several API formats and model capabilities to support. |
| [On-demand AI tab reorganization](../product.md#automatic-tab-organization) | 7/10 | An explicit full regrouping action alongside automatic updates. |
| [Automatic task-based tab grouping](../product.md#automatic-tab-organization) | 6/10 | Inferring intent accurately while keeping the current group stable. |
| [Learning from manual adjustments](../product.md#automatic-tab-organization) | 5/10 | Distinguishing lasting preferences from one-off changes. |
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
