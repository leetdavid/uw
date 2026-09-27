---
name: linear-cli
description: Manages Linear issues, projects, and teams through the local Linear CLI instead of browser automation. Use when the user asks to create, update, triage, or inspect Linear work items.
---

# Linear CLI

Use the `linear` CLI for all Linear work. Do not use browser automation for Linear.

## Authentication

Check authentication before the first Linear action:

```sh
linear auth list
```

If no workspace is configured, instruct the user to run this in their terminal and wait for confirmation:

```sh
linear auth login
```

Never ask the user to provide an API key in chat or pass it on the command line.

If macOS keychain storage fails because user interaction is unavailable, use the CLI's `--plaintext`
option with a user-provided local key file. Read the key only inside command substitution so it never
appears in command output, then remove the local key file after successful authentication:

```sh
linear auth login --workspace <workspace> --plaintext --key "$(tr -d '\r\n' < <key-file>)"
rm <key-file>
```

## Workflow

1. Discover the relevant command options with `linear <resource> --help`.
2. Read issues, projects, and teams with the CLI before modifying them.
3. Before creating an issue, identify the appropriate project with `linear project list` or `linear project query`.
4. Assign every new issue to its appropriate project using `--project`. If no project fits, leave it unassigned and say so.
5. Make external changes only when the user explicitly requests them.
6. Use `--description-file` for Markdown issue bodies.
7. Return the issue identifier, URL, and assigned project after creating or updating an issue.
