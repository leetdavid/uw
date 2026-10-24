# The uw command is TypeScript with Ink

On 2026-10-24 the user asked for a `uw` command that runs uw's agent from a terminal, with a Claude Code-like interface for approvals. We decided to write it in TypeScript with [Ink](https://github.com/vadimdemedes/ink), the React library for terminal interfaces that Claude Code uses, and to build it into one program with Node's single executable applications. It ships inside uw.app.

The command cannot use the user's own Node: fnm sets it up only in interactive shells, so scripts and launchd would not find it. The binary carries its own Node. That adds about 100 MB to uw.app, about 40 MB in the compressed download, and a Node to keep patched. In return, text input, menus, spinners, and Markdown come from packages instead of uw's own code, and the interface is React, as the [chat page](0006-the-chat-page-is-react-with-fluid-functionalism.md) is.

The command answers approvals, so code in its bundle could approve a step. That trusts every package in it, as the chat page does. uw pins each at an exact version. Before updating one, read what changed.

Considered options: C++ with FTXUI, built by GN beside uw (rejected: no Markdown, and much more interface code of uw's own, though it is small and needs nothing new); Rust with ratatui, as Codex's CLI uses (rejected: a third language, and its crates would have to be vendored into Chromium's Rust setup); the user's installed Node (rejected: missing outside interactive shells).
