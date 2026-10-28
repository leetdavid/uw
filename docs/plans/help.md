# Help

Status: scope agreed on 2026-10-28. Nothing is built.

Implementability: **7/10** using the [roadmap convention](roadmap.md#implementability-estimates).

- The site is 8/10. Fumadocs gives the pages, search, `llms.txt`, a Markdown copy of each page, and the MCP route. Whether it works with the Next.js 16.4 that `site/` uses, and how much of its look must change for `uw-arc-style`, are unknown until T232.
- The connector is 7/10. A connector that comes with uw, cannot be removed, and never asks for approval is new to `Connectors` and the approval rules.
- Help is only useful while it is right. That depends on each change updating it, which is a rule, not code.

On 2026-10-28 the user asked for published docs that uw's agent can look up, as Claude Code looks up its own. [Help](../CONTEXT.md#language) is those pages. The design docs in `docs/` stay in the repo.

## Agreed scope

### Pages

- Help is how-to pages for people using a release of uw. It describes only what is built and released.
- The pages are MDX in `site/`, served at `uwbrowser.com/help` by [Fumadocs](https://www.fumadocs.dev/docs/integrations/llms), from the same Vercel project as the home page. Fumadocs gives search, `llms.txt`, `llms-full.txt`, and each page's Markdown at its address plus `.md`.
- English only. There is one version, for the latest release. A page notes the version a feature arrived in when it is later than 0.1.0, such as "In uw 0.3 and later".
- The first pages come from the "What works now" sections of [Try the current build](../try-uw.md), reworded for someone using a release instead of a dev build. Then `try-uw.md` is deleted: "Start uw" moves to the README's [Using your local build](../../README.md#using-your-local-build), and its links point to Help.
- The pages look like the rest of the site, in `uw-arc-style`.

### The MCP route

- `uwbrowser.com/api/mcp` serves Help over MCP's Streamable HTTP, with Fumadocs' tools to list, search, and read pages. Any agent can add it.
- The address never changes, since builds of uw carry it ([ADR 0021](../adr/0021-the-agent-reads-help-from-uwbrowser-com-through-a-connector.md)).
- It records nothing: no PostHog events and no saved searches. Vercel's standard request logs are all there is. Help's web pages count visits in PostHog as the home page does.

### In uw

- uw comes with a remote connector, "uw Help", for the MCP route.
- It shows on `uw://ai/connectors`, on by default. It can be turned off, but not removed or edited.
- Its tool calls never ask for approval, in any mode, since they only read public pages.
- Otherwise it follows the [connector rules](connectors.md#in-a-chat): the instructions list its tools, and the agent loads them when it needs them.
- The agent's instructions tell it to search Help before answering a question about uw itself, and to say when Help does not cover something instead of guessing.
- The Help menu gets "uw Help", which opens `uwbrowser.com/help` in a new tab, in place of Chromium's help item. "Report an issue" (T221) stays.

### Outside agents

- `uw mcp serve` offers the same Help tools by passing their calls through to the MCP route.
- Agents that read the web, such as Claude Code, can fetch `uwbrowser.com/llms.txt` and the `.md` pages.

### Keeping Help current

- A change that adds or changes something users see updates its Help page in the same commit. A ticket is not `done` until it has. T232 adds this rule to [AGENTS.md](../../AGENTS.md).
- Help on `main` can describe features that are not released yet. So the site is deployed after a release is published on GitHub, as the last of the README's [Publishing a preview](../../README.md#publishing-a-preview) steps. Anyone deploying the site between releases first checks that Help on `main` describes nothing unreleased.

### Not in this plan

- Help for the dev build or for unreleased features.
- Help per version, in other languages, or offline.
- Recording what people search for.
- A Help page inside uw.

## Design

Help is drawn on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Help", in `uw-arc-style`: a Help page with its section list and search box, and search results.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T231: the design, signed off by the user.
2. T232: Help in `site/` with Fumadocs, search, `llms.txt`, and `.md` pages. The first pages come from `try-uw.md`, which is then deleted. Adds the AGENTS.md rule and the deploy step.
3. T233: the MCP route.
4. T234: the uw Help connector and the agent's instruction.
5. T235: Help → uw Help.
6. T236: Help in `uw mcp serve`. Needs T197.

## Acceptance checks

1. `uwbrowser.com/help` shows every first page in the signed-off design, and search finds a page by a word in its body.
2. `uwbrowser.com/llms.txt` lists every page, and each page's address with `.md` returns its Markdown.
3. An MCP client given `{"type": "http", "url": "https://uwbrowser.com/api/mcp"}` lists, searches, and reads Help. PostHog receives no event from those calls.
4. A new profile's `uw://ai/connectors` lists uw Help, on, with no Remove or Edit. Turned off, it is neither listed to the agent nor loaded.
5. In Ask first, a chat asked "how do I hide the sidebar?" searches Help with no approval card and answers from the page. A browser test plays the MCP route on a local server.
6. Help → uw Help opens `uwbrowser.com/help`.
7. After `claude mcp add uw -- uw mcp serve`, Claude Code searches Help through uw.
8. `pnpm run check`, `pnpm -F @uw/site lint`, `pnpm -F @uw/site build`, and the focused test suites pass. Browser tests stay headless.

## Open questions

None.
