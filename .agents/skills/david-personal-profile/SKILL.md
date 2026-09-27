---
name: david-personal-profile
description: Uses David's persistent, isolated browser profile for personal web work done at his explicit direction. Use when a task needs David's authenticated browser session for a website or personal account.
---

# David Personal Profile

Use the persistent browser profile at `~/.agent-browser/profiles/david` only for work the user has explicitly authorized. It retains the user's authenticated state between sessions without exposing their everyday Chrome profile.

## Start A Session

```sh
agent-browser --profile ~/.agent-browser/profiles/david --session david open <url>
```

Whenever the user must see or interact with the browser, always add `--headed`:

```sh
agent-browser --headed --profile ~/.agent-browser/profiles/david --session david open <url>
```

## Guardrails

- Act only on tasks the user has explicitly requested.
- Treat submissions, purchases, messages, edits, assignments, and state transitions as external writes; perform them only with explicit user authorization.
- Do not use this profile when a purpose-built CLI or API is available. For Linear, use the `linear` CLI.
- Never expose credentials, session cookies, or profile contents in tool output or responses.
