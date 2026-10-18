# uw browser

**ur web, ur way**

uw browser is a Chromium-based browser for macOS, in development. It is built around three [main features](product.md#main-features): [automatic tab organization](product.md#automatic-tab-organization), [agent automation](product.md#agent-automation), and [integrations](product.md#integrations) such as a terminal tab.

## Status

[Verification status](../README.md#verification-status) records what has been built and tested. The [board](plans/roadmap.md#board) shows the state of each ticket.

Decisions marked agreed in [Product](product.md) are confirmed. The rest, and the build order, are proposals. The Chromium update process and the local model runtime still need validation or selection.

## Docs

### Start here

- [Product](product.md): the main features, the rest of the browser, and expected behavior.
- [Glossary](CONTEXT.md): product terminology.
- [Roadmap](plans/roadmap.md): build order, completion criteria, implementability estimates, and the ticket board.
- [Try the current build](try-uw.md): manual tests for the built app and its current limits.

### Automatic tab organization

- [Native tab tree plan](plans/native-tab-tree.md): tasks as native tab groups, page nesting, stages, and acceptance checks.
- [Automatic organization plan](plans/automatic-organization.md): the tab-filing loop, the decision model and language model, and acceptance checks.
- [Horse tab organization research](research/horse-tab-organization-research.md): documented tree behavior and open decisions for uw's nested organization.
- [Chromium tab API research](research/chromium-tab-api-research.md): reusable controls, integration points, and constraints at the pinned revision.
- [ADR 0001: classification requests include page excerpts](adr/0001-classification-requests-include-page-excerpts.md): why classification calls send a short readable-text excerpt.
- [ADR 0002: tasks are native tab groups](adr/0002-tasks-are-native-tab-groups.md): why uw stopped drawing its own task containers.
- [ADR 0003: a decision model sits beside the language model](adr/0003-decision-model-beside-the-language-model.md): which providers decide tab placement and how.
- [ADR 0010: the tab tree is saved in Chromium's session file](adr/0010-the-tab-tree-is-saved-in-chromiums-session-file.md): why the tree rides in Chromium's restore instead of a sidebar file of uw's own.

### Agent automation

- [Agent chats plan](plans/agent-chats.md): chats with a built-in agent, scheduled chats, approvals, releases, and stages.
- [Agent engine spike](research/agent-engine-spike.md): uw drives Chromium's agent engine without Gemini's panel, and what it cannot do yet.
- [ADR 0006: the chat page is React with Fluid Functionalism](adr/0006-the-chat-page-is-react-with-fluid-functionalism.md): why one WebUI page is a React bundle built beside Chromium's own WebUI tooling.

### AI providers

- [AI providers plan](plans/ai-providers.md): providers saved once, the `uw://providers` page, and a model and effort picker in each chat.
- [ADR 0007: uw speaks three wire formats to language models](adr/0007-uw-speaks-three-wire-formats-to-language-models.md): chat completions, Anthropic Messages, and OpenAI Responses.
- [ADR 0008: uw reads what a model can do from models.dev](adr/0008-uw-reads-what-a-model-can-do-from-models-dev.md): tool calls and effort levels come from a public catalog, not from testing each model.

### Integrations

- [Terminal tab plan](plans/terminal-tab.md): a shell in a regular tab drawn by ghostty-web, the spike that comes first, stages, and acceptance checks.
- [Terminal spike](research/terminal-spike.md): what ghostty-web and a browser-started shell did in the two checks, and the gaps uw has to cover.
- [Native libghostty terminal research](research/native-libghostty-terminal.md): what a native Ghostty view would need and when to reopen it.
- [ADR 0004: the terminal draws with ghostty-web in a page](adr/0004-the-terminal-draws-with-ghostty-web.md): why a terminal tab is a page and not a native Ghostty view.

### Look and controls

- [Arc-style look plan](plans/arc-style-look.md): the tint and its text color rule, page card, tab rows, favorites tiles, task folder rows, and the screenshot review.
- [Themes plan](plans/themes.md): an Arc-style theme of up to three colors per profile, its picker, black dark mode, a page mode apart from the window's, and a toolbar in each page's color.
- [Keyboard and mouse plan](plans/keyboard-and-mouse.md): menu bar entries and keys for every command, branch drags and collapse, mouse actions, and the sidebar as a keyboard tree.
- [Sidebar menus plan](plans/sidebar-menus.md): the task menu and the Organize menu as uw cards, and naming a task on its row.
- [Command bar plan](plans/command-bar.md): one panel on Cmd+T and Cmd+L for commands, tasks, tabs, and the web, ranked by use, that can also send the typed text to a chat or a terminal.
- [Command bar spike](research/command-bar-spike.md): what the panel, the searchbox handler, and headless tests did, and the keyboard checks still open.
- [ADR 0005: the command bar replaces typing in the address bar](adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md): why Cmd+T, Cmd+L, and the address open uw's own panel.
- [Arc pinned tabs research](research/arc-pinned-tabs-research.md): Favorites, home URLs, reset behavior, and the limits of documented cross-window sharing.
- [Logo](logo.md): the mark, its colors and files, and what is still undecided.

### Building

- [Building and using Chromium](../README.md#build-host-requirements): setup, build and run commands, CI, and verification status.
- [Architecture](architecture.md): technical direction and decisions still to make.
- [Chromium code organization research](research/chromium-organization-research.md): Brave and Helium source layouts, patch workflows, and maintenance tradeoffs.
- [Chromium customization layouts](research/chromium-customization-layouts.md): an earlier, shorter pass over the same Brave and Helium layouts.
