# Primary Agent Directives

- Focus on simplicity and minimal moving parts. Avoid unnecessary complexity, frameworks, and dependencies.
- Keep the initial implementation small. Add shared packages when a real consumer needs them.
- If a plan conflicts with existing directives, ask for clarification before proceeding. Do not assume that a plan overrides the directives.
- Code is a liability. Avoid writing code that is not explicitly requested. Do not implement a feature or change a plan without explicit approval.
- Delete code that is no longer used or requested. Do not leave dead code in the repository.
- Rely on tooling and framework defaults. Avoid custom implementations unless explicitly requested.
- NEVER try to build something if something similar already exists in the framework or ecosystem. Ask for guidance if unsure.
- When writing documentation, it should be in clear, concise, commonly used language. Your job is that the reader understand, not to impress with technical jargon. Avoid unnecessary complexity in documentation.
- When a new plan is proposed, always run `wide-grilling-with-docs` skill with an implementability rating out of 10. Ask for approval before proceeding with implementation.

# Project guidance

uw browser means "ur web, ur way". It is a planned Chromium-based browser with AI automation.

- Start with [docs/README.md](docs/README.md).
- The main features are automatic tab organization, agent automation, and integrations such as a terminal tab. See [Product](docs/product.md#main-features). The README's verification status and the [board](docs/plans/roadmap.md#board) say what is built.
- Never write project-owned Python code, including build helpers and tests. Use TypeScript for repository tooling and C++ for Chromium changes.
- Chromium's upstream build tools require Python. Make that dependency explicit; do not claim the entire build chain is Python-free.
- Use pnpm for dependencies and package scripts. Keep `pnpm-lock.yaml` as the package lockfile.
- Use [README.md](README.md) for build-host requirements, setup, building, running, and verification.
- Keep decisions and requirements in `docs/`. Put implementation plans in `docs/plans/`.
- Keep Chromium edits in `chromium/patches/` with explicit `series` order. Follow the source-organization guidance in `docs/architecture.md` as new feature code is added.
- Distinguish agreed requirements from proposals and open questions. Do not present planned features as working software.
- Include implementability scores out of 10 in feature discussions. Use the [roadmap's scoring convention](docs/plans/roadmap.md#implementability-estimates) and update its estimates when scope changes.

## Revamp decisions

Agreed on 2026-10-11. Where older docs disagree, these win until the docs are updated.

- Rewrite the tab-tree and automatic-organization C++. Keep the product docs and build tooling.
- A task is a native Chromium tab group. uw adds page-parent indentation, the organizer, undo, and settings.
- Ungrouped tabs are the Scratchpad.
- Tasks do not nest inside tasks. Page tabs still nest under their parent page.

## Designs

uw's designs live on the Doop canvas `0yLEIltMCP`. Reach it through the `doop` MCP server (`https://doop.design/mcp`). Use pages to design and test different features.

The aesthetics should be like Arc browser. The canvas style guide `uw-arc-style` holds the palette, sizes, and rules.

## Managing the kanban

The kanban is the [ticket board](docs/plans/roadmap.md#board) in the roadmap. Keep its status column current.

- Set a ticket to `in progress` when work starts, `in review` when the change is ready to verify, and `done` once it is verified.
- Update the status in the same change as the work it describes.

## Commits

- Commit your changes after every change, without waiting to be asked.
- Use one line: `<type>(<scope>): <short lowercase imperative summary>`, such as `feat(organizer): file terminal tabs by title and folder`.
- Commit only your own changes. Other sessions may have uncommitted work in the same files; leave their lines out.

## Writing docs

- Get to the point. Use short sentences, short sections, and plain language.
- State each requirement once. Link to it elsewhere instead of repeating it.
- Include details that guide a decision or implementation. Cut filler, marketing claims, and speculative features.
- Prefer concrete behavior and acceptance criteria over vague goals.
- Use sentence case headings and small lists or tables when they make scanning easier.
- Update existing docs before creating overlapping ones. Keep the docs index current.
