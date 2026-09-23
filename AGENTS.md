# Primary Agent Directives

Do NOT edit this section yourself, ever. This part is managed by the human.

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
- The current implementation scope is the Chromium build foundation, minimal uw branding, and native vertical tabs by default. Later product features remain planned. See the README for verification status.
- Never write project-owned Python code, including build helpers and tests. Use TypeScript for repository tooling and C++ for Chromium changes.
- Chromium's upstream build tools require Python. Make that dependency explicit; do not claim the entire build chain is Python-free.
- Use pnpm for dependencies and package scripts. Keep `pnpm-lock.yaml` as the package lockfile.
- Use [README.md](README.md) for build-host requirements, setup, building, running, and verification.
- Keep decisions and requirements in `docs/`. Put implementation plans in `docs/plans/`.
- Keep Chromium edits in `chromium/patches/` with explicit `series` order. Follow the source-organization guidance in `docs/architecture.md` as new feature code is added.
- Distinguish agreed requirements from proposals and open questions. Do not present planned features as working software.
- Include implementability scores out of 10 in feature discussions. Use the [roadmap's scoring convention](docs/plans/roadmap.md#implementability-estimates) and update its estimates when scope changes.

## Writing docs

- Get to the point. Use short sentences, short sections, and plain language.
- State each requirement once. Link to it elsewhere instead of repeating it.
- Include details that guide a decision or implementation. Cut filler, marketing claims, and speculative features.
- Prefer concrete behavior and acceptance criteria over vague goals.
- Use sentence case headings and small lists or tables when they make scanning easier.
- Update existing docs before creating overlapping ones. Keep the docs index current.
