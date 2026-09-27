# Automatic organization MVP: approved plan

Status: approved. The loop below is part of the [minimum usable version](../product.md#minimum-usable-version). It replaces the earlier two-provider slice, preserved on the `wip/automatic-organization-prototype` branch.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Scores include integration, provider contracts, accuracy, latency, and maintenance, not progress.

## Agreed scope

- One configured LLM classifies and names in one [classification call](../CONTEXT.md#language). Connection modes: OpenAI-compatible (base URL, model ID, API key) and Ollama (endpoint, model picker, no key). Managed local models and Anthropic-format APIs are deferred.
- Automatic organization stays disabled until a connection validates. There is no local fallback; without a validated LLM every tab stays in Scratchpad.
- A classification call contains tab title, URL, source-tab relationship, current task names, the user's manual placement when one exists, and a short readable-page-text excerpt. See [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md).
- One call returns the placement decision — leave in Scratchpad, move the whole branch to an existing task's top level, or create a new root-level task — a confidence value, and the new task's name when creating one.
- Automatic moves and task creation require confidence at or above `0.90`. Lower results do nothing.
- A new task appears immediately with the [provisional name](../CONTEXT.md#language) `New task`. The call's name replaces it; a missing or invalid name leaves the provisional name. There is no later automatic renaming.
- Continuous reclassification: link opens and title or URL changes trigger background requests, debounced 750 ms after the latest change. Each request captures branch identity, activity revision, and manual-adjustment generation. Late, stale, or overridden results are discarded; closing a branch or moving it to another window invalidates pending work.
- Manual moves and renames hold until that tab's next debounced title or URL change. While held, requests report the manual placement so the model moves the branch only on a clear activity change.
- The move unit is a whole tab branch. Destinations are shallow: an existing task's top level or a new root-level task.
- Every automatic action records one undoable organization action covering task creation, branch moves, and the generated name. It shows a brief sidebar message with Undo and never steals focus or blocks navigation. Undo stays available through the persistent "Undo tab organization" command and Cmd-Z after the message disappears. New tabs are preserved; closed pages are never resurrected.
- The sidebar Organize menu provides "Pause for 1 hour", "Pause until next launch", "Pause indefinitely", and "Resume automatic organization" per [Product](../product.md#organization-controls).
- API keys are stored in macOS Keychain. Endpoints and model IDs live in profile preferences. Keys never appear in logs, preferences, session data, or crash reports.
- The setup dialog shows the exact request scope before the user enables automatic organization.
- Provider outage, malformed responses, and slow responses never block browsing. Tabs stay where they are.

## Non-goals

- Jev fast classification and the separate task naming call. They return in a later stage.
- Managed local model runtime and Anthropic-format providers.
- Nested automatic destinations or automatic reparenting beneath another page.
- Automatic merging, splitting, or whole-window reorganization passes.
- User-directed proposals and "Reorganize all tabs".
- Revising a generated task name after creation.
- Automatic tab closing and suspension.

## Design

- `components/tab_organization/` owns a provider-independent coordinator behind a narrow fakeable decision interface. It applies only validated, current decisions to `TabTree`.
- The protocol is one OpenAI-compatible chat-completions request with a structured response. Parsing is strict and rejects malformed input so a bad response can never move a tab. Ollama uses its OpenAI-compatible endpoint.
- Excerpts reuse upstream content extraction (`components/content_extraction/content/browser/inner_text.h`).
- `TabTree` distinguishes automatic placements from manual adjustments. Automatic moves and task creation record one undoable organization action; generated naming amends that action without a second undo entry.
- Browser integration owns profile prefs, Keychain secrets, `SimpleURLLoader` lifecycle, debounce timers, revision tracking, pause state, the setup dialog, and the sidebar message. Window-owned `TabOrganizationController` remains the only mutation seam.
- Narrow Chromium patches wire prefs registration, the setup surface, and controller hooks. Product C++ stays in tracked `browser/` and `components/` directories per [code organization](../architecture.md#code-organization).

## Acceptance checks

1. A validated connection enables automatic organization. Without one, every tab stays in Scratchpad.
2. Opening a link in a new tab moves its whole branch to the matched task's top level, or creates a root-level task named by the call. An invalid name leaves `New task`.
3. A title or URL change starts a new request revision. An older response cannot move the tab.
4. A manual move or rename blocks automatic results for that tab until its next debounced activity change; requests sent while held report the manual placement.
5. A result with confidence below `0.90` does nothing and creates no task.
6. Closing a branch or moving it to another window turns its pending responses into no-ops.
7. One undo reverses task creation, branch moves, and generated naming together, and remains available after the sidebar message disappears. New tabs are preserved.
8. Pause stops automatic placements and application of pending results across all windows; resume restores them. Manual tree controls keep working while paused.
9. Provider outage, malformed responses, and slow responses never block browsing.
10. Each request contains the documented scope only. No API keys or request payloads appear in logs, preferences, session data, or crash reports. The setup dialog shows the scope before enabling.
11. `pnpm run check` passes. `//chrome/test:unit_tests` passes `TabTreeTest.*`, `OrganizationCoordinatorTest.*`, and `AiProtocolTest.*`. `//chrome/test:browser_tests` passes `TabOrganizationControllerBrowserTest.*`.

## Later stages

- Jev fast classification and the separate task naming call (prototype preserved on `wip/automatic-organization-prototype`).
- 200 ms fast-path placement with measured provider latency ([Product](../product.md#organization-latency)).
- User-directed proposals and "Reorganize all tabs".
- Managed local runtime and Anthropic-format providers.
- Revising generated task names when a task's activity clearly changes.

Update the roadmap with measured accuracy, confidence calibration, and latency before expanding scope.
