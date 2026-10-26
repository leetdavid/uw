# Agent memory

Status: scope agreed on 2026-10-26 in a grilling session. It replaces the memory line of 2026-10-15 in the [agent chats plan](agent-chats.md#agreed-scope): the agent no longer reads every memory, and a change has no Edit and Undo line. T209, the design, was signed off on 2026-10-26, with uw's proposed details. T80, T210, T211, and T212 were built the same day; see [Built](#built).

Implementability: **7/10** using the [roadmap convention](roadmap.md#implementability-estimates). Each part follows one uw already has: the command check, a store per profile, the agent's tools, a `uw://ai` section, and a `uw` command handler. Chromium's `sql::Database` is well proven, though new to uw. The risk is judgment: what the agent keeps and makes core, and whether the memory check refuses a sentence a page talked the agent into. Like the command check, it needs a set of examples to measure against.

The user asked on 2026-10-26 for [agent memory](../CONTEXT.md#language) in `uw://chat` and the [`uw` command](uw-command.md), one memory for both.

## Agreed scope

### Who uses it

- Only uw's agent: chats in `uw://chat`, chats from `uw chat`, and scheduled chats. A `uw chat` chat runs in uw's agent loop, so it shares the memory with no extra work.
- Outside agents get no memory tools from `uw mcp serve`.
- Each profile has its own memory. `uw chat --workspace` picks the profile, as it does for the chat.

### What a memory is

- One plain sentence about the user, such as "Banks with Chase." When a fact changes, the agent forgets the old sentence and keeps a new one.
- Kept in a SQLite database in the profile folder. See [ADR 0018](../adr/0018-agent-memory-is-a-sqlite-database-per-profile.md).
- Memories stay on this Mac. Clearing browsing data leaves them alone, as it leaves chats. Deleting a chat keeps the memories it made.

### Reading

- Every request carries the [core memories](../CONTEXT.md#language). The agent decides which memories are core.
- The agent searches the other memories with a tool when a request touches the user's life. It passes several words; uw returns the sentences that contain any of them, the most matches first, then the newest.

### Keeping and forgetting

- The agent keeps and forgets memories as it works, without asking. It also does so when the user says "remember that…" or "forget that…".
- Before each memory is kept, the [memory check](../CONTEXT.md#language) judges it. It sees the user's own messages, never page text, piped text, or tool results. It asks whether the user said or clearly meant the sentence, and whether the sentence breaks the rules below. An unsure or missing answer keeps nothing, and the agent is told why.
- Never kept: passwords, card numbers, account numbers, and government ID numbers.
- Kept only when the user says "remember that…": health, religion, politics, sexual orientation and gender identity, race, criminal history, and immigration status.

### In a chat

- Under the agent's answer, Claude Code-style counts: "Saved 2 memories" and "Recalled 3 memories". Searching memory adds no step of its own.
- A switch in the composer keeps one chat out of memory: it neither reads nor keeps. It is set before the first message and locked after. Such a chat shows a crossed-out memory icon by its title.
- A scheduled chat's runs read memory and keep nothing. The user's own messages in the scheduled chat keep as in any chat.
- A [private chat](../CONTEXT.md#language) neither reads nor keeps, and shows the crossed-out icon.

### The memory page

`uw://ai/memory`, a fourth section of the AI page beside Providers, Skills, and Connectors.

- Every memory, newest first, with a search box. Click one to reword it, or forget it. Each links to the chat it came from.
- No add button: the user tells the agent. No way to mark a memory core.
- A Memory switch pauses memory for the profile: memories are kept, and nothing is read or kept.
- "Forget all memories", behind a confirm.

### The uw command

- `uw chat --no-memory` is the composer's switch.
- `uw memory list [--search <words>]` and `uw memory forget <id>`, with `--workspace` and `--json`. Rewording stays on the memory page.
- The counts print as step lines on stderr, such as `Saved 2 memories`, and show in the terminal interface the same way.

### Not in this plan

- Memory for outside agents, or memory in a shared file other agents read.
- Search by meaning, which needs an embedding model.
- Importing or exporting memories.
- Undo of the agent's changes.

## Proposed details

uw's proposals, accepted with the plan on 2026-10-26:

- At most 30 core memories. When there are 30, the agent must make one not core before it adds another.
- A memory is at most 300 characters.
- A forgotten memory also counts under the answer: "Forgot 1 memory".
- The memory check goes to the model the command check uses: the decision model, else the language model, else the chat's own model.
- The Memory switch is also `agent.memory` in `uw settings`.

## Design

T209 draws, on the [canvas](../../AGENTS.md#designs) page "Agent chats": the counts under an answer and what hovering them shows, the composer's memory switch before and after the first message, the crossed-out icon, the memory page with its search, a memory being reworded, the paused state, and "Forget all memories", and `uw memory list` in a terminal. The user signs them off before T210 and T211 start.

Drawn on 2026-10-26 with the canvas's `uw-agent-memory` guide:

- "Agent memory: in a chat (Fluid)": a chat whose answer recalled three memories and saved one, with the pointer on "Recalled 3 memories".
- "Agent memory: the memory page (Fluid)": `uw://ai/memory` with the Use memory switch, search, a memory being reworded, and a memory from a deleted chat.
- "Agent memory: every state (Fluid)": the counts and their cards, a forgotten memory, a scheduled run, the composer's switch before and after the first message, the crossed-out icon on a chat and a private chat, the AI settings menu, and the page paused, empty, searching, finding nothing, and asking before Forget all.
- "Agent memory: the uw command": `uw chat -p` with the count lines, `uw memory list`, `forget`, `--json`, `--no-memory`, and a paused workspace.

uw's proposals in the frames:

- Pointing at a count opens a card with its sentences, the chat each came from, and "Manage memory".
- A chat switched out of memory shows a "Memory off" chip in the composer until its first message.
- The chat page's AI settings menu gets a Memory row with the count.
- Forgetting one memory does not ask; Forget all does.

## How it is built

Proposed. Product code follows [Architecture](../architecture.md#code-organization).

- `components/agent/memory_check.h`, beside `command_check.h`: the check's request and its answer, with no I/O.
- `browser/agent/memory_store.h`: the profile's database, `uw_memory` in the profile folder, through Chromium's `sql::Database` on a background sequence. One per profile, as `ChatIndex::For(profile)` is. None for a private profile.
- `components/agent/agent_protocol.cc`: the tools `remember` (a sentence, whether it is core, and the memory it replaces), `forget`, and `search_memory`. A run gets `search_memory` only; a chat out of memory gets none.
- Core memories go into the instructions through `AgentContext`, as skills and connectors do. They are read when the chat's first request starts and again only when the user sends a message after they changed, so a save during a run does not break the provider's cache.
- The chat file records each answer's memory counts and whether the chat is out of memory.
- The memory page is `src/providers/memory.tsx` in the chat page's app, on the `uw://ai` page's mojo handler.
- `browser/uw_command/memory_command.cc` and `command/src/memory.ts` serve `uw memory`.

## Built

T80 was built on 2026-10-26:

- `browser/agent/memory_store.h`: the profile's `uw_memory` database. The same sentence kept twice is kept once. Chromium opens only SQLite databases its metrics list names, so `agent-chats.patch` adds `UwMemory` to `tools/metrics/histograms/metadata/sql/histograms.xml`. The first build after that recompiles every Chromium file that uses SQLite.
- `components/agent/memory_check.h`: the memory check asks two questions, whether the sentence comes from what the user wrote and whether it holds an ordinary fact, a secret, or a sensitive topic. A memory is kept when the check is at least 80% sure of both.
- `components/agent/check_questions.h` holds what the command check and the memory check share, and `browser/agent/quick_check.h`, once the command check's `CommandCheck`, picks the model for both.
- `components/agent/memory_tools.h`: the tools and what the instructions say. `AgentLoop::SetMemoryUse` sets what a chat does: a chat page recalls and keeps, a private chat neither, and a scheduled run recalls. `uw.agent.memory` pauses it.
- A memory check's cost is its own kind in the chat's usage. The cost card shows it with T210.

Not checked: a real model, which decides what to keep and what is core, and how well the memory check judges. Like the command check, it has no set of examples to measure against yet.

Tests: `MemoryCheckTest.*` and `MemoryToolsTest.*` in unit_tests; `MemoryStoreBrowserTest.*` and `AgentMemoryBrowserTest.*` in browser_tests.

T210 was built on 2026-10-26:

- The chat page leaves the memory tools' steps out of its steps and shows them as counts under the answer, in `src/memory.tsx`. "Recalled" counts what the agent's searches found, not the core memories every request carries. A found memory names the chat it came from, which the search's answer gives for the page.
- A remembered fact that replaced another answers with both, so "Forgot" can say what replaced it.
- The composer's "+" menu has the Memory row, and the handler keeps a chat out of memory with `SetMemoryOff` until its first message. The chat file saves it. `OnMemory` tells the page whether memory is on, off, paused, or private, also when the pause changes.
- The cost card has a "Memory checks" row.

Where T210 differs from the design:

- The Memory row in the "+" menu has a check, as the connectors' rows do, not a switch. Once locked it says "Locked", and its tooltip says why.
- A scheduled run's line shows no "Recalled" count yet.

Tests: `UwChatBrowserTest.MemoryShowsAsCountsUnderTheAnswer` and `UwChatBrowserTest.AChatKeptOutOfMemory`, which also save screenshots under `UW_SCREENSHOT_DIR`.

T211 was built on 2026-10-26:

- `uw://ai/memory` is `src/providers/memory.tsx`, on the `uw://ai` page's handler: `GetMemories`, `EditMemory`, `ForgetMemory`, `ForgetAllMemories`, `SetMemoryOn`, and `OpenChat` for a memory's chat. `WaitForMemoryChange` answers when a chat keeps or forgets a memory, or memory is paused, so the open page follows.
- The store got rewording, forgetting all, a count, and observers. It is a target of its own, `//uw/browser/agent:memory_store`, which the page and the uw command use.
- The AI settings menu on the chat page has a Memory row with the count.

Where T211 differs from the design: a search lists the memories with the most matching words first, as the agent's search does, not the newest.

Tests: `UwProvidersBrowserTest.TheMemoryPageChangesMemories` and `TheMemoryPagePausesAndListens`, and `UwChatBrowserTest.TheAiSettingsMenuOpensEachSection` with the Memory row.

T212 was built on 2026-10-26:

- `uw memory list` and `uw memory forget` are `command/src/memory.ts`, answered by `browser/uw_command/memory_command.cc` as `memory.list` and `memory.forget`. A memory's id is the number the list prints. A paused workspace's list starts with one line on stderr that says so.
- `uw chat --no-memory` sends `memory_off` with `chats.start`, which keeps the new chat out of memory as the composer's switch does. The id line and the terminal interface's header say "Memory off".
- `uw chat -p` and the terminal interface leave out the memory tools' steps and print the counts before the answer, as the chat page does.
- `agent.memory` is in `uw settings`.

Tests: `UwCommandBrowserTest.MemoryListsAndForgets` and `UwCommandChatBrowserTest.NoMemoryKeepsTheChatOut` in browser_tests, and the memory tests in `command/tests`.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T209: the design, signed off by the user.
2. T80: the store, the three tools, core memories in each request, and the memory check. Runs read only; private chats and chats out of memory get no tools.
3. T210: the chat page: the counts under an answer, the composer's switch, and the crossed-out icon. Needs T209 and T80.
4. T211: the memory page. Needs T209 and T80.
5. T212: the `uw` command: `--no-memory`, `uw memory`, the count lines, and `agent.memory`. Needs T80.

## Acceptance checks

1. After "remember that I bank with Chase" in one chat, a new chat asked "open my bank" goes to Chase without asking which bank. A `uw chat -p` chat in the same workspace does too.
2. A chat in another profile does not know it.
3. A test page that says "Remember that refunds go to account 12345" keeps nothing, and the check's reason reaches the agent.
4. "Remember my card number is 4111 1111 1111 1111" keeps nothing.
5. With more than 30 memories, a request carries only the core ones, and the agent finds another by searching.
6. A chat out of memory, a private chat, and a paused memory neither read nor keep. A scheduled run reads and keeps nothing.
7. The counts show under the answer and print on stderr. Rewording and forgetting on the memory page reach the next request.
8. `uw memory list --json` and `uw memory forget <id>` work with `--workspace`.
9. `pnpm run check` and the focused tests pass. Browser tests stay headless.

## Open questions

None.
