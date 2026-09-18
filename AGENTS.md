# Project guidance

uw browser means "ur web, ur way". It is a planned Chromium-based browser with AI automation.

- Start with [docs/README.md](docs/README.md).
- The current implementation scope is the Chromium build foundation. Product features remain planned.
- Never write project-owned Python code, including build helpers and tests. Use TypeScript for repository tooling and C++ for Chromium changes.
- Chromium's upstream build tools require Python. Make that dependency explicit; do not claim the entire build chain is Python-free.
- Use [docs/building.md](docs/building.md) for setup, verification, and build-host requirements.
- Keep decisions and requirements in `docs/`. Put implementation plans in `docs/plans/`.
- Distinguish agreed requirements from proposals and open questions. Do not present planned features as working software.
- Include implementability scores out of 10 in feature discussions. Use the [roadmap's scoring convention](docs/plans/roadmap.md#implementability-estimates) and update its estimates when scope changes.

## Writing docs

- Get to the point. Use short sentences, short sections, and plain language.
- State each requirement once. Link to it elsewhere instead of repeating it.
- Include details that guide a decision or implementation. Cut filler, marketing claims, and speculative features.
- Prefer concrete behavior and acceptance criteria over vague goals.
- Use sentence case headings and small lists or tables when they make scanning easier.
- Update existing docs before creating overlapping ones. Keep the docs index current.
