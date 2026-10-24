# Connect dialog

Status: agreed on 2026-10-22. T158, the fixes, was built on 2026-10-23, and T159, the catalog and the dialog, on 2026-10-24; [Built](#built) lists where they differ. The rest is not built. The stages are T157 to T163 on the [board](roadmap.md#board).

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). The hard parts:

- Plugins come in two outside formats that can change without notice.
- The MCP Registry is slow to search and can fail partway through a download.
- Each service's developer console is different, and Google asks for sign-in only at the first tool call.

## Why

On 2026-10-22 the user's Linear connector never started. They had pasted the JSON from Linear's README, which runs `npx -y mcp-remote https://mcp.linear.app/mcp`. uw started it through a login shell that is not interactive. The user's Node is set up by fnm in `.zshrc`, so that shell found `/usr/local/bin/node` v18.6.0 instead, and `mcp-remote` stopped with `ReferenceError: File is not defined`. The connector's row showed only `Node.js v18.6.0`.

uw can sign in to `https://mcp.linear.app/mcp` by itself, but nothing in the paste box said so. The goal is the experience of Grok Bot's "Connect Apps": find a service, click Add, sign in.

## Agreed scope

This builds on the [connectors plan](connectors.md). Where the two disagree, this plan wins.

### The dialog

- "Add" on `uw://ai/connectors`, and "Add connector…" in the composer's "+ → Connectors" menu, open the connect dialog.
- From top to bottom: a search box, Recommended, search results, and "Add by address or JSON", which opens today's paste box.
- Each entry shows a logo, a name, one line about it, and Add. An entry already added shows "✓ Added" and a menu with "Add another account".
- The Connectors page stays the list of what is added. A row that came from a plugin says what it added and what uw skipped, such as "PostHog · connector, 161 skills".
- Logos come from the registry entry's icons, or else the favicon of its website.

### Recommended connectors

A [recommended connector](../CONTEXT.md#language) goes on the list only after two checks:

1. Its address is on the service's own domain, speaks Streamable HTTP, and offers a sign-in uw can do. A script reruns this check before each release.
2. Someone has signed in with a real account and uw listed its tools.

The first list. Each passed check 1 on 2026-10-22; check 2 is for the user.

| Service | Address | Sign-in |
| --- | --- | --- |
| Linear | `https://mcp.linear.app/mcp` | uw registers itself |
| Granola | `https://mcp.granola.ai/mcp` | uw registers itself |
| Notion | `https://mcp.notion.com/mcp` | uw registers itself |
| PostHog | `https://mcp.posthog.com/mcp` | uw registers itself |
| Vercel | `https://mcp.vercel.com` | uw registers itself |
| Railway | `https://mcp.railway.com/mcp` | uw registers itself |
| Exa | `https://mcp.exa.ai/mcp` | none |
| tldraw | `https://tldraw-mcp-app.tldraw.workers.dev/mcp` | none |

GitHub, Slack, Gmail, and Google Calendar join once [uw's own sign-in apps](#uws-own-sign-in-apps) work. These also passed check 1 and can join when someone with an account signs in: Sentry, Atlassian, Figma, Stripe, Supabase, Canva, Neon, Intercom, Monday, Netlify, Webflow, and Zapier.

### Search

- Search covers three sources. uw keeps a copy of each on disk and searches it as the user types:
  - The [MCP Registry](https://registry.modelcontextprotocol.io), web servers only: the latest version of each entry with a `streamable-http` remote. On 2026-10-22 it held over 3,000 current entries, about 9 in 10 of them web servers. Its search took 20 to 45 seconds a query, and some timed out. Listing took about 2 seconds per 100 entries.
  - Claude's plugin directory, [`anthropics/claude-plugins-official`](https://github.com/anthropics/claude-plugins-official), with 315 plugins.
  - OpenAI's, [`openai/plugins`](https://github.com/openai/plugins), with 62 plugins. ChatGPT and Codex share it.
- uw refreshes the copy about once a day in the background. From the registry it asks only for what changed since the last refresh (`updated_since`). A refresh that fails partway carries on from the page it reached; on 2026-10-22 page 31 of a full listing failed.
- One entry per service. When both directories have a plugin for it, Claude's wins. A registry server whose address a plugin uses shows only as that plugin.
- Servers that only run as a package on this Mac are left out. The paste box still adds them.

### Adding

- Add starts the connector. When its server asks for sign-in, the sign-in tab opens at once.
- A registry server that declares headers, such as an API key, asks for each one in a field described by the registry. The values go to Keychain, as pasted ones do.
- The connector's name comes from its plugin or registry entry, such as `linear`.
- "Add another account" asks for a name, filled in as `linear-2`, then opens sign-in. A second account shares its plugin's skills.
- When the user asks a chat to connect a service, or a request needs one that is not added, the agent can open the dialog with the search filled in. Adding stays the user's click.

### Plugins

A [plugin](../CONTEXT.md#language) adds a service's connector, skills, or both in one step. uw reads two formats rather than defining its own; see [ADR 0015](../adr/0015-uw-reads-claudes-and-openais-plugin-formats.md).

- **Claude's:** `.claude-plugin/plugin.json`, `.mcp.json`, `skills/<name>/SKILL.md`, and `commands/<name>.md`.
- **OpenAI's:** `.codex-plugin/plugin.json`, `.mcp.json`, and `skills/<name>/SKILL.md`.
- uw takes the web connectors in `.mcp.json`, the skills, and the commands. A command becomes a [skill only the user starts](connectors.md#skills-only-you-start), shown in the `/` menu as `/notion:<command>`.
- uw skips hooks, agents, OpenAI's `.app.json`, and any `oauth` block in `.mcp.json`, which holds another company's sign-in app. The plugin's row lists what was skipped.
- **In a chat:** the instructions list each plugin that is on as one line saying what its skills cover. One call loads the plugin's skill list, together with its connector's tools, the way the `connector` tool loads tools today. The user's own skills stay listed one by one.
- **Updates:** an added plugin follows its directory's pinned commit and updates in the daily refresh, and its row says when. An update that changes a connector's address asks the user to sign in again.

On 2026-10-22, for the user's services: Claude's directory has Linear, Notion, PostHog (161 skills), Vercel (44), Railway, Exa, GitHub, and Slack. OpenAI's adds Gmail, Google Calendar, and Granola.

### uw's own sign-in apps

GitHub, Slack, Gmail, and Google Calendar let no app register itself. uw signs in to them with apps the user registers once for themself and their org; see [ADR 0014](../adr/0014-uw-signs-in-with-its-own-apps-kept-out-of-the-repo.md).

| Service | Address | App |
| --- | --- | --- |
| GitHub | `https://api.githubcopilot.com/mcp/` | A GitHub OAuth app |
| Slack | `https://mcp.slack.com/mcp` | An internal app in the user's workspace. Slack lets only Marketplace and internal apps use it. |
| Gmail | `https://gmailmcp.googleapis.com/mcp/v1` | A Google OAuth client marked Internal to the user's Workspace org, with the Gmail and Calendar MCP APIs turned on |
| Google Calendar | `https://calendarmcp.googleapis.com/mcp/v1` | The same Google client |

- The return address is uw's `http://127.0.0.1:47651/callback`, caught in the sign-in tab. Check that Slack accepts it before building; Slack may require https.
- The client IDs and secrets come in at build time from outside the repository, which is public, as Chromium's own Google keys do.
- Google's servers answer the first message without sign-in. uw asks for sign-in when a later request is refused.
- An Internal Google app works only for accounts in that org. A personal Gmail account would need Google's testing mode, which asks for sign-in again every 7 days.

### Fixes

- **The terminal's environment.** When uw starts, it reads the environment once from `$SHELL -i -l`, with a 10-second limit, as VS Code does. Local connectors start with it through `/bin/sh`, with the pasted `env` on top; a login shell would read the profile again, and macOS's `path_helper` there can put another PATH first. If reading it fails, uw starts them through the login shell as before, and the Connectors page says so. This replaces the connectors plan's environment rule.
- **`mcp-remote`.** A pasted `npx`, `bunx`, or `pnpm dlx` command that runs `mcp-remote <url>` becomes a web connector at that address, and its `--header` arguments become headers. Saved ones convert when uw reads its list, keeping their names and switches.
- **`/sse`.** A pasted address ending in `/sse` is tried with `/mcp` in its place and kept if it answers. Otherwise the paste box says why uw can't use it.
- **Failures.** A failed local connector's row names the error it printed: the last line with `Error:` or `error:` in it, or else its last line. Its page shows the last lines of its log, with Show log, which opens the log in a tab, beside Open in terminal.
- **Logs.** The first connector added on a new profile keeps its log; its folder used to be made only after the log was opened. The race where removing a connector and quickly adding one with the same name deletes the new log is not built: it needs both within milliseconds, and it was not what hid the user's Linear log on 2026-10-22.

### Not in this plan

- Searching for servers that only run as a package on this Mac.
- Cursor's plugin directory.
- Hooks and agents from plugins.
- Publishing uw's apps for everyone, which needs Slack's Marketplace review, Google's verification, and Gmail's yearly security assessment.
- X and Origin, which had no web server on 2026-10-22.

## Design

T157 draws the dialog on the [Doop canvas](../../AGENTS.md#designs), on the "AI providers" page, in Fluid like the connectors frames. The user signs it off before T159 starts. It shows Recommended, search results, an entry adding and signing in, ✓ Added with its menu, a key field, Add another account, a plugin's row on the Connectors page, and a failed connector with its log lines.

## Built

T159, built on 2026-10-24:

- `components/connectors/catalog.h`, without I/O: the Recommended list, the registry's pages, both directories' plugins, search, and the connector an entry makes. `browser/connectors/connector_catalog_store.h` keeps the copy in `connectors/catalog.json` in the user data folder and refreshes it. The dialog is `src/providers/connect-dialog.tsx`.
- A plugin pinned to a commit is read once; one that follows a branch is read at each refresh.

Where the build differs from the design and the scope above:

- The copy refreshes when the dialog opens and it is a day old, not in the background at launch: starting it at launch would reach the network from every browser test.
- Logos are drawn from simple-icons' paths in the page's script, and Exa's from @lobehub/icons-static-svg, for the services the dialog knows by name. Registry servers and other plugins show a globe. Shipping each logo as a file overran the page's resource ids, and more ids would rebuild much of Chromium.
- The dialog's header says how many connectors are added, without their logos.
- "✓ Added" has no menu until T162 adds Add another account. Skill and command counts come with T160.
- "Add connector…" in the composer opens `uw://ai/connectors?add`, the Connectors page with the dialog open.
- Registry servers and plugins are listed only at https addresses.

Tests: `ConnectorCatalogTest.*` and `ConnectorConfigTest.ABareAddressIsNamedForItsSite` in unit_tests; `ConnectorsBrowserTest.TheCatalogCarriesOnWhereARefreshStopped` and `UwProvidersBrowserTest.TheConnectDialogAddsFromTheCatalog` in browser_tests.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T157: the design.
2. T158: the fixes. It does not need the design.
3. T159: the catalog: the local copy and its refresh, search, Recommended, Add with sign-in, key fields, and the dialog.
4. T160: plugins.
5. T161: the agent opens the dialog.
6. T162: Add another account.
7. T163: uw's own sign-in apps.

## Acceptance checks

1. The user's saved `npx mcp-remote` Linear connector becomes a web connector at `https://mcp.linear.app/mcp` and signs in from its row. A local connector that needs fnm's Node starts.
2. Typing "linear" in the dialog shows Linear at once, offline too. Add opens sign-in, and Linear's tools work in a chat.
3. Adding PostHog adds its connector and skills. A chat's instructions list PostHog as one line, and a request about feature flags loads its skills.
4. Notion's commands show in the `/` menu as `/notion:<command>`.
5. "Add another account" on Linear adds `linear-bilby` beside `linear`, each with its own sign-in.
6. Asking a chat to "connect Granola" opens the dialog with "Granola" in the search.
7. GitHub, Slack, Gmail, and Google Calendar sign in with uw's apps and list their tools.
8. A refresh stopped partway carries on and finishes. Tests play the registry and both directories locally.
9. `pnpm run check` and the focused test suites pass. Browser tests stay headless.
