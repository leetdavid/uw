# A command check judges the agent's terminal commands

On 2026-10-20 the user asked for uw's agent to run commands on this Mac, with Claude Code's auto mode as the model for what asks. The agent runs commands in a terminal tab under its chat, in the user's login shell, and any command is allowed. In Auto, a separate model call, the [command check](../CONTEXT.md#language), judges each command before it runs; a dangerous, unsure, or missing answer asks the user. The check sees the user's messages and the agent's actions, never page text or command output, so a page or a file cannot argue a command through. In every mode, a command that may read a [sensitive file](../CONTEXT.md#language) asks, so the check runs in Full auto too, for that question alone.

This departs from Claude Code in two places, at the user's choice: a dangerous command asks instead of being refused with the reason sent back to the agent, and reading a `.env` file asks instead of being allowed.

Considered options: the agent marking its own commands as changing something, as it does for clicks (rejected: a page that tricks the agent tricks the mark); a fixed list of dangerous commands (rejected: anything off the list runs); every command asking in Auto (rejected: the user wanted Claude Code's auto mode); commands run without a tab, shown only in the chat (rejected: nothing to watch or step into).

Consequences: each command waits for one quick model call, and how well the check judges depends on the model the user picked; it needs a set of example commands to measure it against. A tricked check can still let a harmful command run in Auto. The user's own allow, ask, and never rules come later.
