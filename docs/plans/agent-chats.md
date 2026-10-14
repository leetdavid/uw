# Agent chats

Status: scope agreed on 2026-10-14. Implementation started the same day at the user's request, before the [sorting bar](#before-v2-the-sorting-bar) was met. Stages run one ticket at a time, each with its own tests.

Implementability: **4/10** for v2.0 using the [roadmap convention](roadmap.md#implementability-estimates), up from 3/10 once the [engine spike](../research/agent-engine-spike.md) passed. Chromium's agent engine is internal code that can change with every Chromium update. Approvals partly rely on the model saying a step changes something.

## Agreed scope

- A [chat](../CONTEXT.md#language) is a conversation with uw's built-in agent. Its page, `uw://chat`, is a regular tab.
- The agent opens its tabs as child tabs of the chat tab, in the same window. They load in the background and never take the user's selection or keyboard focus. The user can click one to watch or step in.
- While a chat runs, its tab and its child tabs move only together. After it finishes they are ordinary child tabs.
- Every tab is a tab: the organizer may move a chat tab like any other.
- The agent asks for [approval](../CONTEXT.md#language) before each change on a site and each sign-in. Page text never counts as the user's instruction.
- Settings get an "Agent model" connection beside the decision and language models. It starts as a copy of the language model. uw checks that the model takes tool calls and images.
- The agent may use the user's browsing history, the titles and addresses of open tabs, [agent memory](../CONTEXT.md#language), and saved passwords and passkeys.
- [Scheduled chats](../CONTEXT.md#language) replace page watches and keep the agreed [watch rules](../product.md#scheduled-chats). A step that needs approval waits and sends a notification.
- A [private chat](../CONTEXT.md#language) keeps nothing after the private windows close.
- No cap on chats running at once. A chat the user starts is their own request, not background automation, so it never waits its turn.
- Chats run on this Mac. Cloud runs are planned for later.
- [Outside agents](../CONTEXT.md#language) reach the same tools through uw's own tool server, in v2.2.

## Releases

| Release | Contents |
| --- | --- |
| v2.0 | Engine spike, Agent model setting, tools, agent loop, chat page, approvals, Ask box on the new tab page, Chats list, history and open-tab access, scheduled chats, private chats. |
| v2.1 | Chat cards on the new tab page, giving the agent a tab (it works in place, with Stop and Take over), agent memory, passwords and passkeys. |
| v2.2 | Outside agents on the tool server. |
| Later | Cloud runs. |

## Before v2: the sorting bar

Agreed before v2 started. The [organizer](automatic-organization.md) in-review tickets move to `done`, then a week of daily use meets this bar:

- 7 days with no crash.
- Under 5% of naming calls without a usable name.
- A request that fails on a network change is retried once.
- Every undo is logged, and each wrong move the user notices is fixed at its cause.

## Design

- **Engine.** Chromium's agent engine, `chrome/browser/actor`, compiled into uw and on by default on macOS. uw creates tasks and sends tool requests to it directly, without Gemini's panel. The [spike](../research/agent-engine-spike.md) showed this works in a background tab. The engine acts only on `https` pages.
- **Tools.** `browser/agent/` holds uw's tools over the engine: open a child tab, navigate, read a page, click, type, scroll, and wait. Each tool takes and returns JSON, so the agent loop and later outside agents use the same calls.
- **Agent loop.** It runs in the browser process. It reuses the model connection and Keychain code in `browser/tab_organization/`, which moves to a shared place when the agent becomes its second user.
- **Chat page.** `uw://chat` is a WebUI page in TypeScript, built like `uw://terminal`.
- **Approvals.** uw asks before every step the model marks as a change. It also asks before steps it can detect on its own: submitting a form, filling a password, and filling payment fields.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T43: engine spike. A browser test drives the engine from uw in a background tab: navigate, read the page, click, and type, while the selected tab stays selected. It passed; see the [findings](../research/agent-engine-spike.md).
2. T44: Agent model setting and its capability check on the settings page. The user can set it and test it in the app.
3. T45: agent tools over the engine, with a browser test per tool.
4. T46: agent loop. A browser test drives it with scripted tool calls from the test model server.
5. T47: chat page, opened from a menu command. The agent's tabs nest under it. This is the first stage the user can try end to end.
6. T48: approvals.
7. T49: Ask box on the new tab page.
8. T50: Chats list in the sidebar, and collapsing a chat's child tabs.
9. T51: scheduled chats.
10. T52: private chats.

## Acceptance checks for v2.0

1. A request typed in the Ask box opens a chat tab. The agent's tabs appear under it and never take the selection or focus.
2. A change on a site and a sign-in wait for Approve. A page that tells the agent to skip approval does not get its way.
3. A chat tab moved by the organizer or by hand takes its running child tabs with it.
4. The Chats list shows a running chat with a spinner and a finished, unread one with a dot.
5. A scheduled chat runs on time while uw is open, waits and notifies at an approval, and runs once to catch up after a miss.
6. A private chat leaves no chat, history, or screenshot behind after its windows close.
7. With an Agent model that lacks tool calls or images, settings say so and chats stay off.
8. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

- Where a scheduled chat runs when its tab was closed or no window is open. Proposal: it reopens its chat tab in the background in the most recent window. With no window open, it waits and runs as a [catch-up run](../CONTEXT.md#language) when a window opens.
- What the model is given to read a page: Chromium's annotated page content, a screenshot, or both.
- Whether the engine's site checks need Safe Browsing on, and how its "sensitive site" prompt maps onto uw's approvals. The spike did not reach either.
- Whether the agent should work on `http` pages such as a local development server. The engine refuses to act on them.
