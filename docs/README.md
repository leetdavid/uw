# uw browser

**ur web, ur way**

uw browser is a Chromium-based browser in development with vertical tabs and AI automation for everyday browsing. [Automatic tab organization](product.md#automatic-tab-organization) is part of the minimum usable version. Planned features include page watches and prompt-built website customizations.

## Docs

- [Product](product.md): experience, headline features, and expected behavior.
- [Glossary](CONTEXT.md): product terminology.
- [Logo](logo.md): the mark, its colors and files, and what is still undecided.
- [Building and using Chromium](../README.md#build-host-requirements): setup, build and run commands, CI, and verification status.
- [Try the current build](try-uw.md): exploratory manual tests for the built app and its current limits.
- [Architecture](architecture.md): technical direction and decisions still to make.
- [Chromium code organization research](research/chromium-organization-research.md): Brave and Helium source layouts, patch workflows, and maintenance tradeoffs.
- [Agent engine spike](research/agent-engine-spike.md): uw drives Chromium's agent engine without Gemini's panel, and what it cannot do yet.
- [Horse tab organization research](research/horse-tab-organization-research.md): documented tree behavior and open decisions for uw's nested organization.
- [Arc pinned tabs research](research/arc-pinned-tabs-research.md): Favorites, home URLs, reset behavior, and the limits of documented cross-window sharing.
- [Roadmap](plans/roadmap.md): proposed build order, completion criteria, implementability estimates, and a ticket board.
- [Native tab tree plan](plans/native-tab-tree.md): tasks as native tab groups, page nesting, stages, and acceptance checks.
- [Automatic organization plan](plans/automatic-organization.md): the tab-filing loop, the decision model and language model, and acceptance checks.
- [Agent chats plan](plans/agent-chats.md): chats with a built-in agent, scheduled chats, approvals, releases, and stages.
- [Arc-style look plan](plans/arc-style-look.md): the tint and its text color rule, page card, tab rows, favorites tiles, task folder rows, and the screenshot review.
- [Terminal tab plan](plans/terminal-tab.md): a shell in a regular tab drawn by ghostty-web, the spike that comes first, stages, and acceptance checks.
- [Chromium tab API research](research/chromium-tab-api-research.md): reusable controls, integration points, and constraints at the pinned revision.
- [Terminal spike](research/terminal-spike.md): what ghostty-web and a browser-started shell did in the two checks, and the gaps uw has to cover.
- [Native libghostty terminal research](research/native-libghostty-terminal.md): what a native Ghostty view would need and when to reopen it.
- [ADR 0001: classification requests include page excerpts](adr/0001-classification-requests-include-page-excerpts.md): why classification calls send a short readable-text excerpt.
- [ADR 0002: tasks are native tab groups](adr/0002-tasks-are-native-tab-groups.md): why uw stopped drawing its own task containers.
- [ADR 0003: a decision model sits beside the language model](adr/0003-decision-model-beside-the-language-model.md): which providers decide tab placement and how.
- [ADR 0004: the terminal draws with ghostty-web in a page](adr/0004-the-terminal-draws-with-ghostty-web.md): why a terminal tab is a page and not a native Ghostty view.

## Status

[Verification status](../README.md#verification-status) records what has been built and tested. The [board](plans/roadmap.md#board) shows what has landed. Later automation and shared-pin features remain planned.

The product direction, headline features, and decisions marked agreed in [Product](product.md) are confirmed. Remaining behavior and build order are proposals. The current approach uses Chromium source customizations; the update process and local model runtime still need validation or selection.
