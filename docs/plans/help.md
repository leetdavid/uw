# Help

Status: scope agreed on 2026-10-28, and the designs, T231 and T241, signed off the same day. T232 is being built.

Implementability: **7/10** using the [roadmap convention](roadmap.md#implementability-estimates).

- The site is 8/10. Fumadocs gives the pages, search, `llms.txt`, a Markdown copy of each page, the MCP route, and most of the page parts below. Fumadocs 16.17.8 works with the Next.js 16.4, React 19.3, and Tailwind 4 that `site/` uses, checked on 2026-10-28. How far its look bends to the website's is unknown until T232.
- The connector is 7/10. A connector that comes with uw, cannot be removed, and never asks for approval is new to `Connectors` and the approval rules.
- Ask in uw is 6/10. uw catches one address before it loads, and a chat that starts with text waiting in its composer is new to the chat page.
- Help is only useful while it is right. That depends on each change updating it, which is a rule, not code.

On 2026-10-28 the user asked for published docs that uw's agent can look up, as Claude Code looks up its own. [Help](../CONTEXT.md#language) is those pages. The design docs in `docs/` stay in the repo.

## Agreed scope

### Pages

- Help is how-to pages for people using a release of uw. It describes only what is built and released.
- The pages are MDX in `site/`, served at `uwbrowser.com/help` by [Fumadocs](https://www.fumadocs.dev/docs/integrations/llms), from the same Vercel project as the home page. Fumadocs gives search, `llms.txt`, `llms-full.txt`, and each page's Markdown at its address plus `.md`.
- English only. There is one version, for the latest release. A page notes the version a feature arrived in when it is later than 0.1.0, such as "In uw 0.3 and later".
- The first pages come from the "What works now" sections of [Try the current build](../try-uw.md), reworded for someone using a release instead of a dev build. Then `try-uw.md` is deleted: "Start uw" moves to the README's [Using your local build](../../README.md#using-your-local-build), and its links point to Help.
- The pages look like the rest of the site.
- From Fumadocs, a page has headings with links, code blocks with a title and a copy button, info and warning callouts, tables, tabs, steps, and figures that zoom when clicked. Search lists chosen pages before anything is typed. A narrow screen gets Fumadocs' top bar, a sidebar that slides out, and "On this page" as a dropdown.
- uw's own parts: key caps such as Cmd S, and the version pill.
- Light or dark follows macOS. A switch in the sidebar picks light, dark, or the Mac's setting. The rest of uwbrowser.com stays light for now.
- Beside "On this page": Copy as Markdown, a menu that opens the page in ChatGPT or Claude and links to its source on GitHub, and [Ask uw about this page](#ask-in-uw). There is no "Edit on GitHub", since the repo takes no pull requests.
- Each page ends with "Was this helpful?": Yes or No, and an optional note. The answer, the note, and the page's address go to PostHog as one event. The box says that the note goes to uw's team.

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

### Ask in uw

- A Help page's actions have "Ask uw about this page". Search ends with "Ask uw: <what was typed>", and when nothing is found that is its main action.
- Each is a link to `uwbrowser.com/ask?q=<text>`. Opened in uw, uw catches that address before it loads and opens a new chat tab beside the current one, with the text in the composer and nothing sent. "Ask uw about this page" starts the text with "About Hide the sidebar: ".
- Any site can link to that address, so it only fills the composer. Nothing happens until the user presses Send ([ADR 0022](../adr/0022-uw-catches-one-help-address-to-open-a-chat.md)).
- In another browser the address loads a page that says the question opens in uw, with the download button.

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
- An AI chat panel hosted on the site. uw's agent and outside agents answer instead.
- Collapsing the sidebar on wide screens, and a banner across the site.

## Design

Drawn on 2026-10-28 on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), beside the website's frames, and signed off by the user the same day. Help uses the living website's look: the dawn hills, glass pills, and lavender tiles. The `uw-help` guide has the rules.

- "Help: the home": `uwbrowser.com/help` with search, a card per section, and a card that gives agents the MCP address and `llms.txt`.
- "Help: a page": "Hide the sidebar" with the section list, a figure, steps, a note, "On this page", and Copy as Markdown.
- "Help: search and every state": search results over a page, no results, a section's version pill, and Copy as Markdown's states.

T241 drew the rest on 2026-10-28, in the same look and beside the first three:

- "Help: a page's parts": "Claude Code and other agents" with a code block under tabs, info and warning callouts, a table, a heading link, the page actions with Open in's menu, "Was this helpful?", and the theme switch.
- "Help: dark": "Hide the sidebar" at night, following the Mac.
- "Help: narrow screens": a page, "On this page" open, and the menu sliding in.
- "Help: Ask uw and search": search before typing, "Ask uw" as the last result, no results, the new chat in uw with the text waiting, and `uwbrowser.com/ask` in another browser.
- "Help: feedback and zoom": the note after No, the thanks, and a zoomed figure.

"Help: a page" gained the theme switch and the page actions in the same round.

## Built

T232 was built on 2026-10-28 in `site/`:

- `content/help/`: one MDX file per page, and `meta.json` for the order and the section names.
- `lib/source.ts`: the Fumadocs source, with each page's last-modified date from git and code blocks in a dark theme. `lib/shared.ts`: the addresses.
- `app/help/`: Fumadocs' provider and `help.css`, which loads on Help pages only. Links between Help and the home page are plain links, so the two never share a page load. The home is `page.tsx`; the pages use Fumadocs' docs layout in `(docs)/`, under the site's nav from `site-nav.tsx`.
- `app/api/search`, `app/llms.txt`, `app/llms-full.txt`, `app/llms.mdx/help/`, and `proxy.ts`, which serves `/help/<page>.md` and answers a request that asks for Markdown with Markdown.
- `components/feedback.tsx` and `lib/feedback.ts`: "Was this helpful?", sent to PostHog's capture API from the server.

Where the build differs from the design:

- The Open in menu is Fumadocs' own: GitHub, View as Markdown, ChatGPT, and Claude, plus Scira AI and Cursor.
- Search is Fumadocs' dialog in Help's colors, without the key hints at its foot.
- No page has a figure yet, so nothing zooms. Fumadocs zooms images; a drawn figure would need its own zoom. See [Open questions](#open-questions).
- No page needs the version pill yet, so it isn't built.
- The agent tile on the Help home lists `llms.txt` and `llms-full.txt`; the MCP address joins it with T233. Ask uw joins the pages with T242.
- "Was this helpful?" does not remember an answer after the page reloads.
- On a phone, the Help home's nav shows the logo and Download only.
- Dark mode takes the night colors of the redesigned home page (`site/app/home.css`): a deep indigo sky, a glowing horizon in place of the darkened dawn, gradient italics, and a white Download button. "Help: dark" on the canvas shows the earlier violet night.
- `try-uw.md` is not deleted yet: other sessions still add manual tests to it as features land. See [Open questions](#open-questions).

Help went live at `uwbrowser.com/help` on 2026-10-28, before a release, at the user's call: some pages describe things the 2026-10-24 prerelease doesn't have.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T231: the design, signed off by the user.
2. T241: the second design round, signed off by the user.
3. T232: Help in `site/` with Fumadocs, search, `llms.txt`, and `.md` pages, the page parts, dark mode, the page actions, and "Was this helpful?". The first pages come from `try-uw.md`, which is then deleted. Adds the AGENTS.md rule and the deploy step.
4. T233: the MCP route.
5. T234: the uw Help connector and the agent's instruction.
6. T235: Help → uw Help.
7. T236: Help in `uw mcp serve`. Needs T197.
8. T242: Ask in uw: uw catches `uwbrowser.com/ask`, the chat opens with the text in its composer, and the site's `/ask` page. Needs T232.

## Acceptance checks

1. `uwbrowser.com/help` shows every first page in the signed-off design, and search finds a page by a word in its body.
2. `uwbrowser.com/llms.txt` lists every page, and each page's address with `.md` returns its Markdown.
3. An MCP client given `{"type": "http", "url": "https://uwbrowser.com/api/mcp"}` lists, searches, and reads Help. PostHog receives no event from those calls.
4. A new profile's `uw://ai/connectors` lists uw Help, on, with no Remove or Edit. Turned off, it is neither listed to the agent nor loaded.
5. In Ask first, a chat asked "how do I hide the sidebar?" searches Help with no approval card and answers from the page. A browser test plays the MCP route on a local server.
6. Help → uw Help opens `uwbrowser.com/help`.
7. After `claude mcp add uw -- uw mcp serve`, Claude Code searches Help through uw.
8. Help is dark when macOS is, and the switch overrides it. A narrow window shows the top bar, the sliding sidebar, and the "On this page" dropdown.
9. "Was this helpful?" sends one PostHog event with the answer, the note, and the page's address.
10. In uw, "Ask uw about this page" opens a new chat tab with "About Hide the sidebar: " in the composer and nothing sent. A browser test checks that the same link from another site also sends nothing. In another browser, `uwbrowser.com/ask` shows the download page.
11. `pnpm run check`, `pnpm -F @uw/site lint`, `pnpm -F @uw/site build`, and the focused test suites pass. Browser tests stay headless.

## Open questions

- How figures are made. Proposed: draw them in MDX with the site's demo sidebar component, `site/app/sidebar.tsx`, as the T231 figure does, instead of screenshots of uw.
- Whether a chat opened by Ask in uw says where its text came from. Proposed: a line above the composer, "A link typed this in. Read it, then press Send.", as "Help: Ask uw and search" shows.
- Whether `try-uw.md` goes once Help is live, as agreed, or stays as the dev build's list of manual tests beside Help. Other sessions still add to it.
