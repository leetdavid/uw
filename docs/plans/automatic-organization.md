# Automatic organization: approved first slice

Status: approved. Implements the first self-organizing loop defined in this plan.

Implementability: **3/10** using the [roadmap convention](roadmap.md#implementability-estimates).
Scores include integration, provider contracts, accuracy, latency, and maintenance, not progress.

## Agreed scope

- Remote Jev classification is required. No local deterministic fallback organizes tabs.
- Jev connects through a user-configured remote endpoint and API key, after explicit opt-in.
- A separate OpenAI-compatible endpoint, model, and API key provides task names.
- Automatic organization stays disabled until both connections validate.
- Jev requests contain metadata only: tab title, URL, source-tab relationship, and current task names. No page text.
- Task-naming requests contain the new task's initial branch metadata only: titles, URLs, and source relationships. No page text.
- Jev returns one structured Choice decision per branch: leave in Scratchpad, move the whole branch to an existing task's top level, or create a new root-level task.
- Jev does not name tasks. A separate LLM call names new tasks.
- A new task appears immediately as `New task`. The naming call renames it later. A naming failure leaves the usable provisional name.
- Continuous reclassification: link opens and debounced title or URL changes trigger background requests.
- Debounce: 750 ms after the latest title or URL change.
- Confidence threshold: automatic moves and task creation require Jev Choice confidence at or above `0.90`.
- Manual moves and task renames hold until that tab's next debounced title or URL change.
- The automatic unit is a whole tab branch. Destinations are shallow: an existing task's top level or a new root-level task.
- Every automatic action shows a brief sidebar message with Undo. It never steals focus or blocks navigation.
- API keys are stored in macOS Keychain. Endpoints and model IDs live in profile preferences. Keys never appear in logs, preferences, session data, or crash reports.
- First release treats Jev as background correction. Late results may still organize a valid branch. The 200 ms visible-placement fast path in [Product](../product.md#organization-latency) remains a future performance milestone.

## Non-goals

- Local model runtime, Ollama, managed downloads.
- Page excerpts in requests.
- Nested automatic destinations or automatic reparenting beneath another page.
- Automatic merging, splitting, or whole-window reorganization passes.
- User-directed proposals, pause controls, reorganize-all in this slice. Those remain in Product and land separately.

## Design

- `components/tab_organization/` owns a provider-independent coordinator behind a narrow fakeable decision interface. It applies only validated, current decisions to `TabTree`.
- `TabTree` distinguishes automatic placements from manual adjustments. Automatic moves and task creation record one undoable organization action. Generated naming amends that action without creating a second undo entry. Manual renames remain independent undo entries.
- Each request captures branch identity, activity revision, and manual-adjustment generation. Late, stale, or overridden results are discarded. Closing a branch, moving it to another window, or changing its activity invalidates pending work.
- Browser integration owns profile prefs, Keychain secrets, `SimpleURLLoader` lifecycle, debounce timers, revision tracking, and the setup dialog. Window-owned `TabOrganizationController` remains the only mutation seam.
- Narrow Chromium patches wire prefs registration, the setup surface, and controller hooks. Product C++ stays in tracked `browser/` and `components/` directories per [code organization](../architecture.md#code-organization).

## Acceptance checks

1. Both providers validate, then automatic organization enables. Incomplete setup leaves every tab in Scratchpad.
2. Opening a link in a new tab moves its whole branch to the correct task, or creates `New task` at root.
3. The naming LLM replaces `New task` with a concise name. A naming failure leaves the provisional name without undoing classification.
4. Low-confidence results do nothing. No naming call is made.
5. A title or URL change starts a new request revision. An older response cannot move the tab.
6. A manual move or rename blocks the outstanding automatic result. The block ends after the next debounced activity change.
7. Closing a branch or moving it to another window turns its pending responses into no-ops.
8. One undo reverses automatic task creation, branch moves, and generated naming. New tabs are preserved. Closed pages are never resurrected.
9. Provider outage, malformed responses, and slow responses never block browsing. Tabs stay where they are.
10. No API keys or request payloads appear in logs, preferences, session data, or crash reports.
11. `pnpm run check` passes. `//chrome/test:unit_tests` passes `TabTreeTest.*`, `OrganizationCoordinatorTest.*`, and `AiProtocolTest.*`. `//chrome/test:browser_tests` passes `TabOrganizationControllerBrowserTest.*`.

## Later stages

- 200 ms fast-path placement with measured provider latency.
- Page-excerpt input scope, pending a separate privacy decision.
- User-directed proposals, pause controls, and reorganize-all.
- Managed local runtime and Ollama naming alternatives.

Update the roadmap with measured accuracy, confidence calibration, and latency before expanding scope.
