# Agent chats

Status: scope agreed on 2026-10-14. Implementation started the same day at the user's request, before the [sorting bar](#before-v2-the-sorting-bar) was met. Stages run one ticket at a time, each with its own tests.

Implementability: **4/10** for v2.0 using the [roadmap convention](roadmap.md#implementability-estimates), up from 3/10 once the [engine spike](../research/agent-engine-spike.md) passed. Chromium's agent engine is internal code that can change with every Chromium update. Approvals partly rely on the model saying a step changes something.

## Agreed scope

- A [chat](../CONTEXT.md#language) is a conversation with uw's built-in agent. Its page, `uw://chat`, is a regular tab.
- The agent opens its tabs as child tabs of the chat tab, in the same window. They load in the background and never take the user's selection or keyboard focus. The user can click one to watch or step in.
- While a chat runs, its tab and its child tabs move only together. After it finishes they are ordinary child tabs.
- Every tab is a tab: the organizer may move a chat tab like any other.
- The agent asks for [approval](../CONTEXT.md#language) before each change on a site and each sign-in, in the default [approval mode](../CONTEXT.md#language), Auto; see the modes added on 2026-10-16. Page text never counts as the user's instruction.
- From T47 until T48 landed on 2026-10-15, chats ran without approvals, on test sites only. Approvals now gate every change.
- Settings get an "Agent model" connection beside the decision and language models. Until the user saves one, chats use the language model's connection. Its test asks the model to open a page and passes only when it calls a tool. A check for images waits until the agent sends screenshots; changed on 2026-10-15, since the agent reads pages as text for now. In v2.1 the agent model moves to `uw://providers`, and each chat can switch its model and effort; see the [AI providers plan](ai-providers.md).
- The agent may use the user's browsing history, the titles and addresses of open tabs, [agent memory](../CONTEXT.md#language), and saved passwords and passkeys.
- [Scheduled chats](../CONTEXT.md#language) replace page watches and keep the agreed [watch rules](../product.md#scheduled-chats). A step that needs approval waits and sends a notification.
- A [private chat](../CONTEXT.md#language) keeps nothing after the private windows close.
- No cap on chats running at once. A chat the user starts is their own request, not background automation, so it never waits its turn.
- Chats run on this Mac. Cloud runs are planned for later.
- [Outside agents](../CONTEXT.md#language) reach the same tools through uw's own tool server, in v2.2.

Added on 2026-10-15:

- uw saves each chat after every step. A chat is kept until the user deletes it from the Chats list; clearing browsing data leaves it alone. Private chats are never saved.
- After a crash, a chat that was running comes back with its tabs, marked "Stopped before it finished", with Continue. It never repeats a step by itself. The same happens when its tab closed or reloaded mid-run.
- The chat page lists the chats on its left. Clicking one goes to its tab, or reopens the tab if it was closed. The Chats list in the sidebar stays as well.
- The chat page shows a card for each of the agent's tabs: its title, what the agent is doing there, and a screenshot from after the last step. Clicking a card goes to the tab. "Side by side" puts the chat and the agent's current tab in Chromium's split view.
- The chat page is a React page built from [Fluid Functionalism](https://github.com/mickadesign/fluid-functionalism)'s components, styled after the [canvas](../../AGENTS.md#designs) style guide. It is redesigned on the canvas first and built once the user signs off.
- The agent can use [skills](../CONTEXT.md#language) in the SKILL.md format: from uw's skills folder, from `~/.agents/skills`, and ones the agent writes in a chat after the user approves. All start on, and the chat page can turn each one off. A skill's scripts do not run.
- The model gets a small context and can still reach everything; see [Context](#context).
- A new chat opens on "What should the agent do?" with three requests to try, and a click puts one in the composer. For now they are the [product's examples](../product.md#chats). Later the language model writes them once a day from the titles of recent chats and the most-visited sites of the past week (T84). Agreed on 2026-10-15.
- The agent's answers are drawn as Markdown: headings, lists, tables, task lists, quotes, code with colors for about a dozen languages, and links. A link opens as a child tab under the chat and is selected; Cmd-click opens it in the background. Only `http` and `https` links work. The user's own messages stay plain text.
- [Agent memory](../CONTEXT.md#language), in v2.1, follows a plain-sentence model: the agent reads every memory at the start of a request, keeps and forgets them without asking, and each change is a line under its answer with Edit and Undo. A switch in the composer keeps one chat out of memory. Memories stay on this Mac, are never taken from page text, and never hold passwords, card numbers, or account numbers. A private chat keeps none. The rest waits for the user's research into how memory is stored.

Added on 2026-10-16:

- The agent may send several steps in one answer when they all use the same reading of a tab, such as filling in a form's fields and then submitting it. uw runs them in order. The first step that fails, or that the user declines, ends the batch: the steps after it do not run, and the model is told so. Each change still asks on its own. The agent reads the tab again after a step that loads a page or changes what it shows. Steps in different tabs do not run at the same time.
- A chat has an [approval mode](../CONTEXT.md#language), picked from a chip in the composer beside the model and effort. Ask first asks before every click and typing; opening, reading, and going to an address do not ask. Auto, the default, asks as above. Full auto never asks, answers yes to Chromium's question about a sensitive site, and turns the chip amber. A change applies from the next step. A chat keeps its mode when it opens again. A mode picked on a new chat before its first message becomes the mode new chats start on; one picked later stays with that chat. In every mode page text never counts as the user's instruction, and the engine acts only while Safe Browsing is on.
- A message can carry images: pasted, dragged onto the composer, or picked with the attach button. Up to 5, each scaled to at most 1,568 px on its long side. They show as Fluid's tiles in the composer and above the sent message. They go to the model with the message and stay through the steps that follow; once the model answers, each shrinks to a line, `[Shrunk. Call recall with image 1 to see it again.]`, and `recall` shows it again in a message of its own. Each image is numbered in the chat, from 1. The chat file keeps them. A model that the model catalog lists without image input cannot take them: the composer says `deepseek-v4-pro can't see images. Pick a model that can.` and sending waits until the model chip holds one that can.
- While the agent works, the composer's button is Stop when the field is empty. Stop drops the waiting model request at once; a click or typing under way finishes first, and the chat says `You stopped the chat.` The next message carries on from there. Enter [steers](../CONTEXT.md#language): the message goes in after the current step, before the next model request, and a request already waiting is sent again with it. Cmd+Enter queues: the message waits as a row above the composer and goes once a run ends with an answer. After a failure or a stop, queued messages stay, to send, edit, or delete. While a step waits for approval, Enter declines it with the message as the note.
- uw sends a macOS notification when a chat's agent answers, fails, or reaches a step that waits for approval, unless the user is looking: the chat tab is showing in uw's active window, which on macOS means uw is in front. A stop sends none. The notification's title is the chat's title; under it, `Answered`, `Didn't finish`, or `Waits for your approval`; then the start of the answer, the error, or the step that waits. Each chat has one notification at a time, and the next replaces it. Clicking it brings uw forward on the chat tab. macOS's own settings for uw turn the notifications off or hide their text; uw has no setting of its own.

Added on 2026-10-20, with Claude Code's auto mode as the model:

- The agent can use a terminal. It runs commands in a [terminal tab](../CONTEXT.md#language) nested under the chat, running the user's login shell in their home folder, with their PATH, tools, and keys. It keeps to one such tab per chat, so `cd` carries over, and opens another only to keep something running, such as a dev server. They stay as ordinary child tabs after the chat, like its web tabs. It never reads or types in a terminal tab the user opened.
- Any command is allowed; approvals decide what runs. The agent can also send keys to a running program, such as an answer to a y/n prompt or keys in vim, and keys are checked like a command.
- The agent reads what its tab shows, as text. It waits up to 2 minutes for a command to finish, or up to 10 if it asks, or not at all. A command still running then keeps running in its tab; the agent goes on, and can read its output later or press Ctrl-C. Output goes to the model like a page reading and shrinks the same way.
- When a command asks for a password, as `sudo` and `ssh` do, the agent stops and hands the tab to the user. It never types a password.
- Ask first asks before every command and every set of keys. In Auto, the [command check](../CONTEXT.md#language) judges each one first, as Claude Code's auto mode classifier does. These count as potentially dangerous: deleting or overwriting, such as `rm`, a `>` redirect, `git reset --hard`, or `git clean`; sending or installing, such as `git push`, uploads, sending a message, `brew` or `npm` installs, or running a downloaded script; and the system and other apps, such as `sudo`, system settings, killing processes, `launchctl`, and `osascript` or Shortcuts acting in other apps. Claude Code's other default blocks count too, such as a force push or printing live credentials. A dangerous, unsure, or missing answer asks the user, with the command, its folder, and the check's reason. Full auto runs commands without asking.
- In every mode, a command that may read a [sensitive file](../CONTEXT.md#language) asks first, naming the file. In Full auto the check runs for that question alone. This differs from Claude Code, which lets its agent read `.env` files.
- The check sees the user's messages and the agent's actions, never page text or command output. [ADR 0012](../adr/0012-a-command-check-judges-the-agents-terminal-commands.md) records why.
- The user's own allow, ask, and never rules, like Claude Code's, come later.
- A chat's tab row in the sidebar ends with the chat's state, as the chat page's list shows it: a spinner while the agent works, an amber dot while a step or a password waits for the user, a pause sign once it stopped early, and a dot for an answer not yet seen. Asked for with T124's sign-off.

## Releases

| Release | Contents |
| --- | --- |
| v2.0 | Engine spike, Agent model setting, tools, agent loop, chat page, short context, saved chats, the redesigned chat page with tab cards, skills, approvals, Markdown answers, Ask box on the new tab page, Chats list, history and open-tab access, scheduled chats, private chats. |
| v2.1 | Chat cards on the new tab page, giving the agent a tab (it works in place, with Stop and Take over), agent memory, [AI providers](ai-providers.md) with a model and effort picker in each chat, passwords and passkeys, [connectors and the AI page](connectors.md). |
| v2.2 | Outside agents on the tool server. |
| Later | Cloud runs. |

## Before v2: the sorting bar

Agreed before v2 started. The [organizer](automatic-organization.md) in-review tickets move to `done`, then a week of daily use meets this bar:

- 7 days with no crash.
- Under 5% of naming calls without a usable name.
- A request that fails on a network change is retried once.
- Every undo is logged, and each wrong move the user notices is fixed at its cause.

## Design

The "Agent chats" page on the [Doop canvas](../../AGENTS.md#designs) holds the designs: a chat at work, an approval, the Ask box on the new tab page, and every state of a chat row with the schedule popover. The four frames marked "(Fluid)" redesign the chat page for T69: the agent at work, side by side, every state, and skills. They replace "Chat: the agent at work" for the chat page. They are proposals until built.

Added on 2026-10-16: "Chat page: images in a message (Fluid)" and "Chat page: images, stop, steer, queue (Fluid)" draw T106 and T107: image tiles in the composer and above a sent message, the attach button, a drop target, the warning for a model that can't see images, Stop, steering, queued messages, and declining a step from the composer. The user approved them before they were drawn.

Added on 2026-10-20 for T124: "Agent terminal: a command waits for you (Fluid)" and "Agent terminal: every state (Fluid)" draw the agent's terminal. They are uw's proposals until the user signs off.

- A command's approval card is the usual card with the label `Terminal · Tab 1 · ~/Downloads`. Its question is "Run this command?", "Let the agent read .env.local?", or "Send keys to the running program?". The command shows in a dark block after a `%` prompt, and the check's reason follows with a shield icon, its first sentence in bold. A sensitive file's card shows a key icon and an amber "Full auto" chip when that mode is on. Keys show as key caps under what the program last printed. The options stay Approve, Not this one, Stop the chat, and a note.
- Steps read "Ran `git status` in tab 1", "Running `pnpm dev` in tab 2 · 2 min" with a spinner, and "Didn't run `rm -rf node_modules` in tab 1" with the user's note under it. Output shows under its step in mono, its first and last lines when long. After a 2-minute wait a quiet line says "Still running after 2 min, so the agent went on."
- A password prompt is an amber banner, "sudo wants your password in tab 1.", with Go to tab 1 and Stop the chat. The chat's row turns amber as for an approval.
- A terminal tab card shows the screen's last lines in mono, the tab's title, and Running, Waits for your approval, or Ran 1 min ago.
- The notes under the composer become "Every click, typing, and command waits for your approval", "Changes on a site and risky commands wait for your approval", and "Full auto: the agent acts without asking, except to read a sensitive file".

How the chat is built:

- **Engine.** Chromium's agent engine, `chrome/browser/actor`, compiled into uw and on by default on macOS. uw creates tasks and sends tool requests to it directly, without Gemini's panel. The [spike](../research/agent-engine-spike.md) showed this works in a background tab. The engine acts only on `https` pages. uw turns off the engine's Gemini-only pieces: the toolbar task icon, the take-over button, the overlay over the agent's tab, and the "Gemini is working" toast. The task icon brought Gemini's button with it, and clicking that button aborted uw on 2026-10-15. The blue icon on the agent's tab stays.
- **Tools.** `browser/agent/agent_tools.h` holds uw's tools over the engine: open a child tab, navigate, read a page, click, and type. Each tool takes and returns JSON, so the agent loop and later outside agents use the same calls. Scroll and wait come when a chat needs them.
- **Reading a page.** `components/agent/page_text.h` turns Chromium's annotated page content into lines of text. Each link, field, and button gets a number, such as `[2] button "Send"`, and the agent clicks or types by that number. A password's value is never shown, and ads are left out.
- **Agent loop.** `browser/agent/agent_loop.h` runs in the browser process. It sends the conversation and the tools to the model in chat-completions form (`components/agent/agent_protocol.h`), runs the tools the model calls, and repeats until the model answers without a tool. It stops a chat that takes 40 turns without an answer. It reuses `RemoteModelClient` from `browser/tab_organization/`; that code and the Keychain code move to a shared place with T44, when the agent also needs the settings.
- **Chat page.** `uw://chat` is a WebUI page in React with Fluid Functionalism's components, in the pnpm workspace package `browser/resources/uw_chat/app`. Chromium's build bundles it with esbuild through `app/bundle.ts`. See [ADR 0006](../adr/0006-the-chat-page-is-react-with-fluid-functionalism.md).
- **Saved chats.** Each chat has an id in its address, `uw://chat/<id>`, so session restore reopens the right chat. `browser/agent/chat_file.h` writes it to `uw_chats/<id>.json` in the profile folder after each step, replacing the file in one step: the lines the page shows, the model's context with every tool result in full, and the agent's tabs by the tab tree's page ids. A step cut short is told to the model as one that may or may not have happened. Nothing is saved in a private window.
- **Markdown.** An answer is drawn by [Streamdown](https://streamdown.ai) inside Fluid's `.typeset` styles, with the `@streamdown/code` plugin limited to shell, JSON, TypeScript, JavaScript, Python, HTML, CSS, SQL, YAML, Rust, Go, C++, and diff. No Mermaid or math plugin: Mermaid writes `innerHTML`, which the page's Trusted Types rules forbid.
  - Prose renders as plain elements so `.typeset` styles it; Streamdown's own classes would win over it. Code blocks keep Streamdown's, with their colors and copy button.
  - `bundle.ts` points the plugin's `shiki` import at `src/code-languages.ts`, which lists the languages, and points `decode-named-character-reference` at its plain build: the browser build decodes `&amp;` through `innerHTML`.
  - The page's script grew from about 0.7 MB to 3 MB: Streamdown is about 0.5 MB and the grammars about 1.7 MB, half of that C++.
- **Suggestions.** `browser/agent/chat_suggestions.h` keeps a new chat's three suggestions in `uw_chats/suggestions.json`. When a chat page opens and they are a day old, it asks the language model again, with the titles of the last 20 chats and the 15 sites visited most in the past week, each site as its title and its address without the query. `components/agent/suggestions.h` builds that request and reads the answer. After an attempt that gives nothing, it waits an hour. A private window, or a profile without a language model or anything to go on, shows the product's examples.
- **Skills.** The agent's instructions list each skill's name and description. A `skill` tool returns a skill's instructions when the agent picks it.
- **Approvals.** uw asks before every click or typing the model marks with `changes`. It also asks before steps it detects on its own: clicking a submit button inside a form, pressing Enter in a form's field, and typing a password or payment details (`PageElement` in `components/agent/page_text.h`). The engine asks too, through the task delegate in `browser/agent/agent_tools.cc`, before it acts on a site that Chromium's list marks as sensitive; uw shows that as the same card. It asks each time the agent comes to that site from another one. The list comes from Chromium's Optimization Hints component. The engine acts only while Safe Browsing is on, and the agent says so when it is off. The chat shows Fluid's Ask User Questions card: Approve, Not this one, Stop the chat, or a note for the agent. A declined step does not happen, and the model is told why. The chat list shows a waiting chat with an amber dot. Only the user answers: page text cannot.
- **Terminal.** Proposed, not built. Three tools: `run` starts a command in the chat's terminal tab, opening one under the chat when there is none, and waits up to its `wait`; `screen` reads a terminal tab; `keys` sends text and named keys such as Enter and Ctrl-C to a running program. The text reaches the shell through the shell host's single write function, where the [terminal tab plan](terminal-tab.md#design) put the approval step. A command has ended when the shell is no longer busy, by the same rule as a [busy terminal](../CONTEXT.md#language). The terminal page turns its screen and recent scrollback into text, without colors, and long output shrinks to its first and last lines. While a command runs with the pseudo-terminal's echo off, it waits for a password: uw tells the agent and asks the user to type in the tab. The command check is a decision model call with two choice questions, "potentially dangerous?" and "may read a sensitive file?", and the language model answers when no decision model is set. A set of example commands, safe, dangerous, and sensitive, measures the check against the user's models.

## Context

The model sees as little as it needs, and a `recall` tool brings back the rest.

- While the agent works on a request, each tab's latest reading stays in full. An older reading of the same tab shrinks to one line naming the step, the page, and its length.
- When the model answers, every bulky tool result from that request shrinks the same way. The user's messages, the answers, and the one-line steps stay.
- `recall` returns an earlier step's result in full, by step number, also after a restart. A recalled page is a past copy: its element numbers cannot be clicked, so the agent reads the tab again to act.
- A request to the model stays under about 200,000 characters. Above that, the oldest readings shrink first.
- When the model refuses a request, the chat shows the model's own error text.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T43: engine spike. A browser test drives the engine from uw in a background tab: navigate, read the page, click, and type, while the selected tab stays selected. It passed; see the [findings](../research/agent-engine-spike.md).
2. T44: Agent model setting and its capability check on the settings page. The user can set it and test it in the app. The card shares the page's Save button; the agent's test never turns automatic organization on or off. An agent model at the language model's endpoint, saved without a key, takes a copy of that key.
3. T45: agent tools over the engine, with a browser test per tool. Built: `AgentToolsBrowserTest.*` and `PageTextTest.*`.
4. T46: agent loop. A browser test drives it with scripted tool calls from the test model server. Built: `AgentLoopBrowserTest.*` and `AgentProtocolTest.*`.
5. T47: chat page, opened from "New chat" in the File menu and the sidebar's "+" menu, or from `uw://chat`. The agent's tabs nest under it, and the first message names the tab. A banner says the chat clicks and types without asking until T48. This is the first stage the user can try end to end.
6. T67: short context, per [Context](#context).
7. T68: saved chats and Continue after a crash.
8. T69: the chat page redesigned on the canvas with Fluid Functionalism's components: the chat list, tab cards, side by side, an approval, a stopped chat, and the skills list.
9. T70: the chat page in React with Fluid Functionalism, with the chat list on its left.
10. T71: tab cards and side by side.
11. T72: skills.
12. T48: approvals.
13. T75: Markdown answers.
14. T83: the chat page brought to the "(Fluid)" designs: steps, the chat's state, the empty chat, and the approval and failure cards.
15. T100: several steps in one answer, per [Agreed scope](#agreed-scope).
16. T105: approval modes, per [Agreed scope](#agreed-scope).
17. T106: images in a message, drawn on the canvas first.
18. T107: stop, steer, and queue.
19. T49: Ask box on the new tab page. The [command bar](command-bar.md)'s "Ask uw" row, T63, starts a chat too. Agreed on 2026-10-14: both stay.
20. T50: Chats list in the sidebar, collapsing a chat's child tabs, and the organizer moving a running chat's tabs only together. Until then the organizer may file the agent's tabs on their own. A chat's child tabs are a [tab branch](../CONTEXT.md#language), so this uses the branch collapse of T57 in the [keyboard and mouse plan](keyboard-and-mouse.md#branches-with-the-mouse).
21. T51: scheduled chats.
22. T52: private chats.

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
- Whether the model also gets a screenshot of the page, beside its text. T46 decides.
- Whether the agent should work on `http` pages such as a local development server. The engine refuses to act on them.
- Whether the agent learns a command's exit status. The shell does not report it without shell integration, which uw does not install. For now the agent can run `echo $?` itself.
- Whether Stop in the chat should also press Ctrl-C in the agent's terminal. Proposal: no. Stop ends the wait, and a running command keeps going in its tab, as a click under way finishes first.
