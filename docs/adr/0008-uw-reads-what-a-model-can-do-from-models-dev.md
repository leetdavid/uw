# uw reads what a model can do from models.dev

Status: agreed on 2026-10-15 for v2.1. Not built.

The chat's model picker has to know whether a model calls tools and which effort levels it takes. The first plan tested each model with a few requests the first time it was picked. On 2026-10-15 the user asked to follow OpenCode instead, which never tests a model: it reads the [models.dev](https://models.dev) catalog, a public list of about 8,900 models that records each one's tool calls and effort levels.

So uw downloads the catalog from `https://models.dev/api.json` the first time the providers page or a model menu needs it, keeps it in the profile, and refreshes it once a day. The request carries nothing about the user. A failed refresh keeps the copy uw has.

- A model the catalog marks without tool calls is shown, dimmed, and cannot run a chat. It can still name tasks.
- Ollama is not in the catalog. Its own `/api/show` says whether a model calls tools and thinks.
- A model the catalog does not know gets its format's usual levels: `low`, `medium`, and `high` over chat completions.
- A level a provider rejects shows the provider's error in the chat.

Considered options: testing each model on first use (rejected: extra requests, a wait before the first message, and less accurate than the catalog); a copy shipped in the build as well (rejected: a build step and app size for the first launch only); a copy in the build only (rejected: a new model would show the wrong levels until a uw release).
