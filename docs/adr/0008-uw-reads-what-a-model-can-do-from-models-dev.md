# uw reads what a model can do from models.dev

Status: agreed on 2026-10-15 for v2.1. Built on 2026-10-16 (T79).

The chat's model picker has to know whether a model calls tools and which effort levels it takes. The first plan tested each model with a few requests the first time it was picked. On 2026-10-15 the user asked to follow OpenCode instead, which never tests a model: it reads the [models.dev](https://models.dev) catalog, a public list of about 8,900 models that records each one's tool calls and effort levels.

So uw downloads the catalog from `https://models.dev/api.json` the first time the providers page or a model menu needs it, keeps it in the profile, and refreshes it once a day. The request carries nothing about the user. A failed refresh keeps the copy uw has.

- A model the catalog marks without tool calls is shown, dimmed, and cannot run a chat. It can still name tasks.
- uw reads the catalog for providers made from the OpenAI, Anthropic, OpenRouter, and Vercel AI Gateway presets, and for a Custom provider whose address is a catalog provider's address. The full address counts first, ignoring a trailing slash; failing that, the one catalog provider on the same host, so `https://api.deepseek.com/v1` finds DeepSeek. A host that several catalog providers share matches only by full address. Changed on 2026-10-16: a Custom DeepSeek provider got the guessed levels below, and DeepSeek treats three of them alike. Providers the catalog lists without an address, such as Groq, Mistral, and xAI, still get the guess.
- A Custom provider with the format "Each model's own", such as a proxy, looks each model's exact id up in the catalog's Anthropic and OpenAI entries. The entry it is found in gives the model's format and its abilities; a model found in neither gets chat completions and the usual levels. A dated or aliased id the catalog does not list falls back that way too. Agreed on 2026-10-16 for T103.
- Ollama is not in the catalog. Its own `/api/show` says whether a model calls tools and thinks.
- The catalog gives some models only an on/off switch or a token budget instead of named levels. uw treats them as having no effort levels, and they run without thinking. These are older models, such as claude-sonnet-4-5.
- A model the catalog does not know gets its format's usual levels: `low`, `medium`, and `high` over chat completions. Over Anthropic Messages and OpenAI Responses it has no effort levels and is sent no effort, so nothing is rejected; it can think once the catalog lists it.
- A level a provider rejects shows the provider's error in the chat.

Considered options: testing each model on first use (rejected: extra requests, a wait before the first message, and less accurate than the catalog); a copy shipped in the build as well (rejected: a build step and app size for the first launch only); a copy in the build only (rejected: a new model would show the wrong levels until a uw release).
