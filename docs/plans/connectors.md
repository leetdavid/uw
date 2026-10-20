# Connectors and the AI page

Status: scope agreed on 2026-10-21 for v2.1. Nothing is built yet. The design comes first, T134, and the user signs it off before T135 to T142 change any page.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). The pages, menus, and skill changes reuse what T72, T77, and T107 built. The hard parts:

- uw writes its own MCP client in C++, since Chromium has none.
- MCP changed shape on 2026-07-28, so the client speaks two versions.
- OAuth sign-in meets each server's quirks only live; tests play local servers and a fake sign-in server.

## Agreed scope

### The AI page

- `uw://ai` replaces `uw://providers`. Its sidebar has three sections, each at its own address: Providers at `uw://ai/providers` (which `uw://ai` also opens) with one provider at `uw://ai/providers/<id>`, Skills at `uw://ai/skills`, and Connectors at `uw://ai/connectors`. Agent memory joins later.
- Providers is today's providers page, unchanged. Skills moves there from the chat page.
- The chat page's sidebar keeps "Skills · N on" and gains "Connectors · N on". Each opens its section, reusing an open `uw://ai` tab as Chromium's Settings does. The settings sidebar's "AI providers" link becomes "AI".
- `uw://providers` does not redirect.

### Skills only you start

- A skill whose SKILL.md front matter says `disable-model-invocation: true` is **only you**: the agent never sees it, and the `skill` tool says there is no such skill. One that says `user-invocable: false` is **only the agent**: it stays out of the `/` menu.
- The skills page lists both kinds. A tag says who starts it: "Only you", with the tooltip "The agent can't use this skill on its own. Type /handoff to start it.", or "Only the agent". The switch works for every skill: off means nobody uses it, and every skill that is on counts in "N on".
- `/` at the very start of the composer opens a menu of every skill that is on and can be started by the user, then the prompts of the chat's connectors. It filters as you type; Tab or Enter picks one. A skill's `argument-hint` shows as faint text after its name. A `/` anywhere else is plain text.
- Sending `/handoff next session does X` gives the model the skill's instructions with the text in place of `$ARGUMENTS`, or after the instructions when there is no `$ARGUMENTS`. The message shows as typed, with a "Used the handoff skill" step under it. For the rest of the chat, the agent may read the other files in that skill's folder.

### Connectors

A [connector](../CONTEXT.md#language) gives the agent more tools. Underneath it is an MCP server.

- **Kinds.** A local connector is a program uw starts and talks to over its input and output. A remote one is a web address uw talks to over Streamable HTTP. The older HTTP+SSE transport is not supported.
- **Versions.** uw speaks MCP 2026-07-28 and 2025-11-25. It first calls `server/discover`; when the server does not know it, uw falls back to `initialize` with 2025-11-25 and keeps the session. It remembers which one worked.
- **Adding.** uw keeps its own list. Add connector takes the JSON every server's README gives: `{"mcpServers": {"name": {"command", "args", "env"}}}` for a local one, or `{"type": "http", "url", "headers"}` for a remote one. uw does not read other agents' lists. A connector can be removed.
- **One list for every profile**, like providers, sign-ins included. Two accounts of one service are two connectors with two names.
- **Keys.** Values in `env` and `headers` go to Keychain and show masked.
- **Sign-in.** A remote connector that asks for OAuth shows "Sign in". uw opens the server's sign-in page in a tab and catches the return address itself. It follows MCP's authorization rules: it finds the sign-in server from the connector's address, registers itself automatically (Dynamic Client Registration), keeps the client ID by the sign-in server's issuer, and uses PKCE. Tokens go to Keychain and refresh on their own; Sign out deletes them. A server that only takes Client ID Metadata Documents shows "This connector's sign-in needs a registration uw doesn't support yet."
- **Addresses.** A remote connector's address is `https`, or `http` on this Mac or a private network, by the [providers rule](ai-providers.md#agreed-scope).
- **Starting.** Every connector that is on starts when uw starts. Adding, editing, or turning one on starts or restarts it at once. One process, or one remote connection, per connector serves every chat. It stops when uw quits or the connector is turned off.
- **Environment.** A local connector starts through the user's login shell, `$SHELL -l -c 'exec <command> <args>'`, so it finds the programs the terminal finds. The pasted `env` goes on top.
- **Failures.** A crash restarts the connector once; after that it shows as failed. A failed connector's row shows its last error line and Open in terminal, which opens a terminal tab running its exact command and environment. Each connector's error output is kept in a log in the user data folder.
- **Tool list.** uw keeps each connector's tools and prompts on disk, so the agent's instructions can list them before the connector starts. They refresh each time it starts and when the server says they changed.

### In a chat

- **Tools on demand.** The instructions list each connector that is on in the chat, with its tools' names and the first sentence of each description. The `connector` tool loads one connector's tools; from the next request on they are real tools, named like `linear__save_issue`, for the rest of the chat. The chat file keeps which connectors were loaded.
- **Tool switches.** Each tool has a switch on `uw://ai/connectors`. A tool that is off is never listed or loaded.
- **Per chat.** The composer's attach button becomes "+", with "Add images" and a "Connectors" menu that has a switch for each connector that is on. A chat's switches are saved with it and count from the next step. A new chat starts with every connector that is on.
- **Approvals.** Ask first asks before every connector tool call. Auto asks before each tool not marked `readOnlyHint: true`. Full auto never asks. The card shows the connector, the tool, and the arguments the model sent. Its button is Allow once, with a caret for Allow for this chat and Always allow; Not this one, Stop the chat, and a note stay as they are. An allow covers one tool of one connector. Allow for this chat is saved with the chat. Always allow shows beside the tool on `uw://ai/connectors`, where it can be taken back. Page steps and terminal commands keep their own cards.
- **Results.** Text goes to the model. Structured JSON goes as text. Images go as message images do: they shrink after the answer and `recall` shows them again. A model that cannot see images gets `[image left out: this model can't see images]`. A result counts like page text: it is never the user's instruction. Bulky results shrink after the answer by the [context rules](agent-chats.md#context).
- **Prompts.** The `/` menu lists a connector's prompts as `/linear:triage`, below the skills. Picking one with arguments shows a row of fields above the composer, one per argument, with its description as the placeholder; Tab moves between them, and Send waits for the required ones. The message shows as `/linear:triage` with the values given; the model gets the prompt's messages as the user's message. A prompt never asks for approval.
- **Private chats** use connectors with the usual approvals. Allow for this chat is forgotten when the private windows close, and Always allow is not offered.
- **Scheduled chats** follow their own rule: a step that waits for approval sends a notification. Allow for this chat and Always allow count there too.

### Not in this plan

- Resources, elicitation, and sampling. uw does not announce support for them.
- Client ID Metadata Documents, which need an address uw means to keep.
- A user's own OAuth client ID.
- "Allow for this chat" on page steps or terminal commands.
- Stopping an idle local connector.

## Design

The canvas page "AI page and connectors" on the [Doop canvas](../../AGENTS.md#designs) will hold the designs: `uw://ai` with its three sections, the connectors view in every state, adding by paste, a connector open with its tools, the approval card's Allow once caret, the "+" menu, the `/` menu with prompt fields, and the skill tags.

Proposed code layout, to settle as each stage starts:

- `components/connectors/`: the MCP client, which turns requests and answers into JSON-RPC for both versions, reads tool lists, prompts, and results, and parses the pasted JSON. No I/O, so unit tests cover it.
- `browser/connectors/`: the local and remote transports, the shared list in local state, Keychain, sign-in, and the service that starts connectors with uw.
- `browser/ui/webui/uw_ai/`: the `uw://ai` page, in the React package of the chat page per [ADR 0006](../adr/0006-the-chat-page-is-react-with-fluid-functionalism.md).

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T134: the design on the canvas.
2. T135: `uw://ai` with Providers and Skills.
3. T136: skills only you start, `user-invocable: false`, and `/name` in the composer.
4. T137: local connectors.
5. T138: remote connectors.
6. T139: connector sign-in.
7. T140: connector approvals and tool switches.
8. T141: connectors per chat.
9. T142: connector prompts in the `/` menu.

## Acceptance checks

1. `/handoff` starts a skill the agent cannot see, and the skills page lists it as Only you.
2. A local connector added by paste, whose command is only on the login shell's `PATH`, lists its tools and runs one from a chat.
3. A remote connector that needs OAuth signs in from `uw://ai/connectors`, and its tools work after uw restarts.
4. In Auto, a read-only tool runs at once and a tool without the hint waits for Allow once; Always allow stops the asking in every chat until it is taken back.
5. A connector turned off in a chat's "+" menu is neither listed nor loaded in that chat.
6. `pnpm run check` and the focused test suites pass. Browser tests stay headless.
