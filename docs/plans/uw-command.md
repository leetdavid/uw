# The uw command

Status: scope agreed on 2026-10-24 in a grilling session. T192, the design, was drawn the same day and waits for the user's sign-off.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). The agent, connectors, runs out of sight, and approvals already exist, and the command reuses them. The new parts are a channel between the command and uw, a Node program built, signed, and shipped inside uw.app ([ADR 0016](../adr/0016-the-uw-command-is-typescript-with-ink.md)), a terminal interface that has to keep up with the chat page, and approvals answered from a second place. Linking `/usr/local/bin/uw` needs macOS's admin prompt.

The user asked on 2026-10-24 for a [`uw` command](../CONTEXT.md#language) that adds connectors, runs the agent they run in `uw://chat`, changes settings, and is how scripts and agents automate uw.

## Agreed scope

### Who it is for

- Scripts and [outside agents](../CONTEXT.md#language) first, the user at a terminal second. Every command takes `--json`. With no terminal attached, a command never waits for input.
- Exit codes: 0 done, 1 failed or stopped, 2 wrong usage, 3 needs the user, for an approval or a sign-in.
- `uw --help` and each command's `--help` show examples.

### Commands

| Command | What it does |
| --- | --- |
| `uw chat` | Starts or resumes a chat. |
| `uw chats` | Lists chats, newest first, with each one's id, title, state, and where it started. |
| `uw approve` | Answers the step a chat waits on. |
| `uw connectors`, also `uw mcp` | `add`, `list`, `remove`, and `sign-in`. |
| `uw settings` | `list`, `get`, and `set`. |

Later: skills, providers, scheduled chats, and opening or reading tabs without the agent.

In v2.2, `uw mcp serve` is the [tool server](agent-chats.md#releases) for outside agents, over the same channel: `claude mcp add uw -- uw mcp serve`. It offers the agent's tools only, never settings or connectors.

### Reaching uw

- Every command asks the running uw. When uw is not running, the command starts it with no window, as scheduled chats run, and uw keeps running afterwards.
- uw listens only once the user installs the command. "Install `uw` command" in uw's settings links `/usr/local/bin/uw` to the program inside uw.app, behind macOS's admin prompt, and opens the channel. Uninstall removes the link and closes the channel. Only the user's own account can reach it.
- The link points into uw.app, so the command updates with uw. When the running uw is older than the command, such as after an update, the command says "Restart uw to finish its update."

### Workspaces

`--workspace <name>` picks where a command applies. The default is the workspace used last.

- A chat runs in that workspace's profile and is saved there.
- A profile's setting applies to its profile, and a theme setting to the workspace.
- Connectors are one list for every profile and ignore it.

### Chats

- A chat from the command is an ordinary saved chat, marked "From terminal" in the Chats list. Its tabs open out of sight, as a scheduled chat's runs do, and close when it ends. The chat keeps the answer and a card of each page.
- `uw chat` in a terminal opens the [terminal interface](#the-terminal-interface). `uw chat -p "request"` prints instead, and so does `uw chat` with no terminal.
- `--mode ask-first | auto | full-auto` sets the [approval mode](../CONTEXT.md#language) of that chat only. Without it, the chat starts on the mode new chats start on. Full auto prints one line of warning first, as uw's chip turns amber.
- `--model` and `--effort` pick the chat's model, as the composer's chips do.
- `-c` continues the latest chat in the workspace, and `-r <id>` resumes any chat, wherever it started. A chat whose tab is open in the user's window works there as usual; one without runs out of sight.
- Piped text with a request argument is attached material, handled like page text: never the user's instruction, and shrunk after the answer. With no argument, the piped text is the request.
- Ctrl-C, a closed terminal, or a killed caller stops the chat, as Stop does. `--timeout <duration>` stops it after that long and exits with 1.
- While the command is attached, uw sends none of the chat's notifications.

### Approvals

- With a terminal attached, `uw chat` and `uw chat -p` both ask there. The choices are those of uw's card for that kind of step: Allow once, Allow for this chat, Always allow for connectors, Not this one with a note, and Stop. Answering in uw works too, and the first answer counts.
- With no terminal, the command prints the waiting step, as `waiting` in `--json`, and exits with 3. The step waits in uw, which sends its notification. `uw approve <chat>` shows the step, and `uw approve <chat> allow` or `uw approve <chat> decline --note "…"` answers it. `uw chat -r <chat> -p` carries on.
- Anything that can run `uw` can approve. Approvals keep page text from steering the agent; they do not guard against programs the user runs. An outside agent that runs `uw approve` still goes through its own permission prompts.

### Printed output

- stdout gets the answer as Markdown text, so `uw chat -p "…" > answer.md` works.
- stderr gets the chat's id, then one line per step, such as `Opened vercel.com/deployments`.
- `--json` prints one object at the end: `{"chat", "status": "answered" | "needs-you" | "failed" | "stopped", "answer", "waiting", "cost"}`.

### The terminal interface

Like Claude Code's, per [ADR 0016](../adr/0016-the-uw-command-is-typescript-with-ink.md):

- The steps as they happen, one line each with a spinner, and the answer drawn as Markdown.
- Approval cards, as above.
- A composer. Enter while the agent works [steers](../CONTEXT.md#language) it, and Esc stops it. `/` at the start opens the skills and connector prompts, as in the chat composer.
- A status line with the model, approval mode, workspace, and cost.
- Ctrl-O opens the chat in uw.

Not yet: images, the per-chat connector switches (every connector that is on is used), a model picker inside, the queue, and tab cards.

### Connectors

- `uw connectors add` takes the arguments of Claude Code's `claude mcp add`, so a README's line works with `claude` swapped for `uw`: `uw mcp add --transport http linear https://mcp.linear.app/mcp`, `uw mcp add files -- npx -y @x/files-mcp`, `-e KEY=value`, and `-H "Header: value"`.
- `-e KEY` with no value reads `KEY` from the caller's environment, as `docker run -e` does. `-e KEY=value` works with a warning, since the value stays in shell history.
- `uw connectors add --json '…'`, or the JSON on stdin, takes what the paste box takes.
- Both follow the paste box's rules in the [connectors plan](connectors.md#connectors) and [connect dialog plan](connect-dialog.md#fixes): keys go to Keychain, and `npx mcp-remote <url>` becomes a web connector.
- `uw connectors sign-in <name>` opens the sign-in page in a tab of the user's front uw window, opening a window if needed, and brings uw forward. It waits, then prints `Signed in to linear.` or the error.
- In a terminal, `add` goes straight to sign-in for a connector that needs one. With no terminal, `add` saves the connector, prints `linear needs a sign-in: run uw connectors sign-in linear`, and exits with 3.
- Help text and uw's pages say "connector". `mcp` is a second name only.

### Settings

- uw's own settings that have a control on uw's pages, by readable names that follow the page's words, such as `organizer.enabled`, `agent.approval-mode`, `theme.grain`, and `command-bar.currency`.
- `uw settings list` shows each one's value and what it accepts, and says when several workspaces share it: `organizer.enabled: shared by Work and Side projects (same profile)`.
- `set` checks a value as the settings page does.
- Left out: Chromium's own preferences, and lists with pages of their own, such as providers, workspaces, and pins.

### Not in this plan

- A private chat from the command.
- A JSON line for each step, as Claude Code's `stream-json` gives.
- A question in uw when a new program connects.

## Design

Drawn on 2026-10-24, T192, on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Agent chats", for the user to sign off. The rules are in the canvas's `uw-command` guide.

- "uw command: a chat in the terminal": steps, a Markdown answer, the composer, and the status line, under `uw mcp list` in the scrollback.
- "uw command: a change waits for you": a page step's approval card, under `uw chats`.
- "uw command: every state": steering, the `/` menu, a terminal command's and a connector tool's cards, `-p` with a terminal, no terminal with `--json` and `uw approve`, Full auto, adding connectors, settings, not installed and out of step, Ctrl-C and `--timeout`, the "From terminal" mark in the chat list, the installed settings row, and macOS's password prompt.
- "uw command: install in settings": the "uw command" section on Settings › System.

uw's proposals in the frames:

- The terminal uses uw's dark palette, whatever the terminal's theme.
- An approval card numbers its choices, as the chat page's terminal card does. A connector card's caret menu becomes two more rows.
- The "uw command" section sits first on Settings › System.
- A chat from the command shows a terminal icon before its time in the chat list.

## How it is built

Proposed. Product code follows [Architecture](../architecture.md#code-organization).

- `components/uw_command/`, without I/O: the messages between the command and uw, and the settings list with names and allowed values.
- `browser/uw_command/`: the channel, a Unix socket in the user data folder that only the user's account can open, served while the command is installed; install and uninstall; and a handler per command over `Connectors`, `ChatIndex`, the agent loop's approvals, and preferences.
- `command/`: the TypeScript program, a pnpm workspace package using Ink, built into one binary with Node's single executable applications. Chromium's build runs its build script, as for the chat page, and puts the binary in uw.app.
- uw starts with no window through Chromium's `--no-startup-window`.
- `UW_USER_DATA_DIR` points the command at another user data folder, such as `uw-dev` during development.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T192: the design, signed off by the user.
2. T193: the channel, install and uninstall, starting uw with no window, and `uw settings` with `--workspace`.
3. T194: `uw connectors` and `uw mcp`: add, list, remove, and sign-in.
4. T195: printed chats: `uw chat -p`, `--json`, the exit codes, `uw approve`, `-c`, `-r`, `--mode`, `--model`, `--effort`, `--timeout`, piped text, and `uw chats`.
5. T196: the terminal interface.
6. T197: `uw mcp serve`, in v2.2 with outside agents.

## Acceptance checks

1. Before it is installed, the program inside uw.app says to install it first, and uw is not listening.
2. With uw quit, `uw settings set organizer.enabled false` starts uw with no window, and the organizer's switch shows off.
3. `uw mcp add --transport http linear https://mcp.linear.app/mcp` signs in from a terminal. With no terminal it exits with 3 and names the sign-in command. A local connector added with `-e KEY` keeps its key in Keychain and lists its tools.
4. `uw chat -p "…" --json` on a test site prints the answer and the result object and exits with 0. The chat shows in the Chats list marked "From terminal", and no tab opened in the user's windows.
5. With no terminal, a step that needs approval exits with 3, and `uw approve <id> allow` with `uw chat -r <id> -p` finishes the chat. In a terminal the same step asks inline, and Allow in uw answers it too.
6. Ctrl-C stops the chat. `--timeout` stops it and exits with 1.
7. Piped text that tells the agent to do something else is not followed.
8. The terminal interface matches the signed-off frames, steers, stops on Esc, and opens the chat in uw on Ctrl-O.
9. `pnpm run check` and the focused tests pass. Browser tests stay headless.

## Open questions

None.
