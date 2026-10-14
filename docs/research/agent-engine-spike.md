# Agent engine spike

Ticket T43 of the [agent chats plan](../plans/agent-chats.md#stages), run on 2026-10-14 against Chromium 153. The question: can uw drive Chromium's agent engine, `chrome/browser/actor`, without Gemini's panel?

Answer: yes, in a headless browser test. `AgentEngineBrowserTest.*` in `browser/agent/agent_engine_browsertest.cc` holds the checks, and they passed 5 repeat runs.

## What works

- The engine is on in uw with no flags. Its feature, `GlicActor`, is enabled by default on macOS, so `ActorKeyedService::Get(profile)` returns the service.
- uw starts a task of its own with `CreateTask`, a `TaskSourceInfo` of type `kExperimentalActor`, and `GetNullEnterprisePolicyChecker()`. No Gemini type is needed. `CreateTaskWithOptions` takes Gemini's types, so uw avoids it.
- `ActorTask::AddTab` gives the task a tab. The engine treats that tab as visible, so it acts in a background tab.
- `PerformActions` ran a navigation, a click, and typing in a background tab. The page received them, and the selected tab stayed selected.
- `RequestTabObservation` returned the page's annotated content and a screenshot of a background tab.

## Limits found

- The engine acts only on `https` pages. It navigates to `http` pages, but a click or typing there ends with `kActionsBlockedForScheme`. Its code also refuses bare IP addresses. So the agent cannot work on an `http://localhost` development server as things stand.
- It is slow. A navigation and three actions took about 11 seconds, and reading a page about 3 seconds. Where the time goes is not measured yet.

## Not tried

- The engine's "sensitive site" prompt and how it maps onto uw's [approvals](../CONTEXT.md#language).
- Whether turning Safe Browsing off blocks actions, as the engine's code suggests.
- Targeting a page element by the node ids in the annotated content, which a model would use, instead of by a point.
- The engine's sign-in and form-filling tools, its tab overlay, and a real, visible window.
- How much of this interface changes in a Chromium update. Every header used is internal to Chromium.
