# uw reads Claude's and OpenAI's plugin formats

On 2026-10-22 the user wanted adding a service to bring its skills along with its connector, as Grok Bot's plugins do. Services already publish these packages for Claude Code, in `anthropics/claude-plugins-official`, and for ChatGPT and Codex, in `openai/plugins`. Both hold a `.mcp.json` of web connectors and `skills/<name>/SKILL.md` files in the format uw reads today. We chose to read both directories, Claude's first when a service is in both, and to take only connectors, skills, and commands.

Considered options: a plugin format of uw's own (rejected: no service would publish one); the MCP Registry alone (rejected: it lists servers, not skills); Cursor's plugin directory, which Grok Bot appears to use (left out: two formats already cover 11 of the user's 14 services); everything in a plugin, hooks and agents included (rejected: hooks run shell commands on the user's Mac and agents need Claude Code's subagents).

Consequences: either company can change its format or move its directory, and uw must follow. Plugin skills are instructions written by the service and updated with each directory pin, without the user reading them. A plugin with a hundred skills costs one line in a chat's instructions until a request loads it.
