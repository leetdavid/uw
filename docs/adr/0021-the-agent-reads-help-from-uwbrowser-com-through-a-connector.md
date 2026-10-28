# The agent reads Help from uwbrowser.com through a connector

On 2026-10-28 the user asked for published docs that uw's agent can look up, as Claude Code looks up its own. [Help](../CONTEXT.md#language) is served at `uwbrowser.com/help`, and uw comes with a remote [connector](../CONTEXT.md#language), "uw Help", for its MCP route at `uwbrowser.com/api/mcp`. The agent reads Help as it reads any connector, so uw needs no new agent tool and no copy of Help in the app. The website, outside agents, and uw's agent all read the same pages.

The cost: the agent reads the latest Help, not Help for the installed version. It needs the network, and the user's searches reach uwbrowser.com. Sparkle keeps most installs current, and pages note the version a feature arrived in. The route records nothing. Builds carry the address, so it can never move.

Considered options: a local connector that runs the bundled `uw` command over Help shipped in the app (rejected: a Node process running whenever uw does). A `help` tool in C++ beside `skill` and `recall` (rejected: not MCP, and a second implementation beside the site's).
