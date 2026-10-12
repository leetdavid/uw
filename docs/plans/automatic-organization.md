# Automatic organization

Status: approved. The loop below is part of the [minimum usable version](../product.md#minimum-usable-version). On 2026-10-11 it moved onto [native tab groups](native-tab-tree.md) and gained a decision model setting. The first implementation is kept in commit `f113a96`.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Scores include integration, provider contracts, accuracy, latency, and maintenance, not progress.

The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), pages "Automatic organization" and "Settings".

## Agreed scope

- Settings hold two model connections. See [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md).
  - **Decision model**, optional. Providers: Jev (TypeSafe System One), the OpenAI Decisions API, or any endpoint that speaks the OpenAI Decisions API at another base URL. It answers with a choice and a confidence, never text.
  - **Language model**. Connection modes: Ollama (endpoint, model, no key) and OpenAI-compatible (base URL, model, API key).
- The decision model decides where a tab branch goes. With no decision model, the language model decides in one [classification call](../CONTEXT.md#language).
- The language model names a task the decision model starts. Without a language model, or when the name is invalid, the task keeps the [provisional name](../CONTEXT.md#language) `New task`. There is no later automatic renaming.
- Automatic organization is off until every configured connection validates. A save that fails validation turns it off.
- "Save and validate" turns automatic organization off, stores both connections, then sends one test request to each connection that is set. The language model counts as set when its model field is filled. With nothing set, it stays off.
- Each connection has "Test connection". It sends one test request for that connection as entered and saves nothing. Beside a decision model, the language model's test asks for a task name; alone, it asks for a placement. Agreed on 2026-10-12.
- A test result says the connection works and how long the answer took, or why it failed: no answer (with the network error), an HTTP error status (with the provider's own message), or an answer uw cannot use. "Details" shows the request uw sent and the start of the answer. An API key never appears there.
- "Save and validate" runs the same tests and shows each result on its connection.
- uw waits 30 seconds for an answer, in a test and while browsing, so a test that passes means the model is fast enough to use.
- A switch at the top of the settings page turns automatic organization off and on. Off sends nothing and keeps both connections. On saves and tests the connections like "Save and validate", and stays off when a test fails. Agreed on 2026-10-12.
- "Load available models" lists a connection's models with `GET <base>/models`. When the endpoint has no such list, the user types the model ID.
- A decision request contains the tab title, URL, source-tab relationship, current task names, and a short readable-page-text excerpt. See [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md).
- A decision is one of: leave the branch in the Scratchpad, move it to the end of an existing task, or start a new task.
- A move or a new task needs confidence at or above `0.90`. Lower results do nothing.
- Link opens and title or URL changes trigger background requests, 750 ms after the latest change. Restored tabs and tabs dragged in from another window wait for their next change. A change invalidates that tab's outstanding request. Closing a branch or moving it to another window invalidates it too.
- A manual move holds the branch until that tab's next title or URL change. A response that arrives after a manual move is dropped. Commands, drags, native group changes, and undo all count as manual moves.
- The move unit is a whole tab branch. It detaches from its old parent.
- Every automatic action is one undoable organization action. It shows a brief message at the foot of the sidebar with Undo, tints the moved tab for a moment, and never takes focus. After the message disappears, "Undo tab organization" in the Organize menu still works. Closed pages are never reopened.
- While a model is being asked about a tab, a line at the foot of the sidebar says so and names the model and the tab. It disappears when the answer arrives. Agreed on 2026-10-12.
- "Show organizer activity" in the Organize menu keeps that line up for a few seconds with what the answer was: left in place, suggested without enough confidence, failed, unreadable, or ignored because the tab changed. It is off by default.
- The Organize menu and the settings page provide "Pause for 1 hour", "Pause until next launch", "Pause indefinitely", and "Resume automatic organization" per [Product](../product.md#organization-controls).
- API keys are stored in the macOS Keychain. Endpoints and model IDs live in profile preferences. Keys never appear in logs, preferences, session data, or crash reports.
- The settings page shows what is sent before the user enables automatic organization.
- Provider outage, malformed responses, and slow responses never block browsing. Tabs stay where they are.

## Non-goals

- A managed local model runtime and Anthropic-format providers.
- Placing a moved branch under a page in the destination task.
- Automatic merging, splitting, or whole-window reorganization passes.
- User-directed proposals and "Reorganize all tabs".
- Renaming a task after it is created.
- Automatic tab closing and suspension.
- The 200 ms placement budget in [Product](../product.md#organization-latency). Measure first.

## Design

- `components/tab_organization/ai_protocol` builds and parses requests in three decision formats: chat completions, the OpenAI Decisions API (`POST <base>/decisions`), and TypeSafe System One (`POST <base>/systemone`). It also builds the naming call. Chat requests allow 4096 answer tokens, because some chat models think before they answer and that thinking counts against the limit. Parsing is strict, so a malformed response can never move a tab.
- `browser/tab_organization/automatic_organizer` is owned by the window. It watches titles and URLs, sends the requests, and applies a decision through the window controller only if the branch is unchanged since the request.
- The organizer keeps the latest automatic change as a notice for the sidebar message.
- Excerpts reuse upstream content extraction (`components/content_extraction/content/browser/inner_text.h`).
- Browser integration owns profile prefs, Keychain secrets, `SimpleURLLoader` lifetime, debounce timers, pause state, the settings page, and the sidebar message.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. Decision formats and the naming call, with unit tests.
2. Organizer on native tab groups: requests, staleness checks, apply, undo, pause.
3. Settings page: decision model, language model, pause, and validation that gates enablement.

## Acceptance checks

1. Automatic organization turns on only after every configured connection validates. A failed save turns it off.
2. With a decision model set, a decision request goes to it in that provider's format. With none, the language model receives a classification call.
3. Opening a link in a new tab moves its branch to the end of the matched task, or starts a task. With a language model the task is named by it; otherwise it is `New task`.
4. A title or URL change invalidates that tab's outstanding request. An older response cannot move the tab.
5. A manual move drops the response in flight and holds the branch until its page changes.
6. A result with confidence below `0.90`, a refusal, or a malformed response does nothing.
7. Closing a branch or moving it to another window turns its pending response into a no-op.
8. One undo reverses task creation, the move, and the name together. It still works from the Organize menu after the message disappears.
9. Pause stops requests and drops responses in flight across all windows. Resume restores them. Manual commands keep working while paused.
10. Each request contains the documented scope only. No API key or request body appears in logs, preferences, session data, or crash reports.
11. `pnpm run check` passes. `unit_tests` passes `PageTreeTest.*` and `AiProtocolTest.*`. `browser_tests` passes `TabOrganizationControllerBrowserTest.*`, `AutomaticOrganizerBrowserTest.*`, and `UwOrganizationPageBrowserTest.*`.
12. "Test connection" tests one connection, saves nothing, and names the reason for a failure. `UwOrganizationPageBrowserTest.*` covers a pass, an unusable answer, an HTTP error with the provider's message, a refused redirect, and a connection that is not filled in.
13. The switch turns automatic organization off without a request and on only after the connections pass their tests again.
14. Each request shows the asking line. With "Show organizer activity" on, a failed request, an unreadable answer, a low-confidence suggestion, and a choice to leave the tab each show their own sentence with the confidence.

## Later stages

- The 200 ms placement path with measured provider latency.
- User-directed proposals and "Reorganize all tabs".
- Managed local runtime and Anthropic-format providers.
- Renaming a task when its activity clearly changes.

Update the roadmap with measured accuracy, confidence calibration, and latency before expanding scope.
