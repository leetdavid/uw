# AI providers

Status: scope agreed on 2026-10-15 for v2.1. The user signed off on the design the same day, with the model check replaced by the [model catalog](../CONTEXT.md#language). T77 built on 2026-10-15, and T78 and T79 on 2026-10-16. Changed on 2026-10-21: the page moves to `uw://ai/providers`, per the [connectors plan](connectors.md#the-ai-page). Changed on 2026-10-29: [quick choices](#quick-choices), not built yet.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Two of the three formats are new to uw, it is a second React page, and the connections users already saved have to move without loss. Quick choices are also **6/10**: probes are new, OpenRouter's System One address is untested, and the organizer must send each request to the right model in its format.

## Agreed scope

- A [provider](../CONTEXT.md#language) is saved once, with its key, on a new page, `uw://providers`. Every model choice in uw picks from these providers, so one key serves every feature that uses that provider.
- The Add provider menu names OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, and Jev, each with its address and format filled in. Custom takes any address, and the user picks its format. Other providers work through OpenRouter, the AI Gateway, or Custom.
- uw speaks three formats to language models: chat completions, Anthropic Messages, and OpenAI Responses. See [ADR 0007](../adr/0007-uw-speaks-three-wire-formats-to-language-models.md). The two decision formats of [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md) stay.
- `uw://providers` holds the provider list and three model choices: the [language model](../CONTEXT.md#language), the [decision model](../CONTEXT.md#language) (optional), and the [agent model](../CONTEXT.md#language). Until an agent model is picked, chats use the language model.
- A model choice is a chip with the provider's icon and the model's name. Beside it, an [effort](../CONTEXT.md#language) chip lists that model's levels, named as the provider names them. The language model and the agent model get one; decision models and models without effort levels have none. A model the catalog gives only an on/off switch or a token budget, such as claude-sonnet-4-5, counts as having no levels and runs without thinking. Agreed on 2026-10-15: these are older models, so uw does not add a chip for them. Until the user picks a level, the chip says `default` and uw sends no effort, so the provider's own default applies.
- The chat composer has the same two chips. The user can switch either one at any time, even in the middle of a chat, and the next request uses the new choice. Each saved chat keeps its own.
- What a model can do comes from the model catalog, per [ADR 0008](../adr/0008-uw-reads-what-a-model-can-do-from-models-dev.md): whether it calls tools, and its effort levels. uw never sends a request just to test a model. Changed on 2026-10-15 from a test on first use, to follow OpenCode.
- A model the catalog marks without tool calls shows dimmed in a chat's model menu and cannot run a chat. For Ollama, which the catalog does not cover, its own model details say whether a model calls tools and thinks. A Custom provider whose address is a catalog provider's address reads that provider's entry, such as DeepSeek's `low`, `high`, and `max`; added on 2026-10-16. A model the catalog does not know is taken to call tools. Over chat completions it gets `low`, `medium`, and `high`; over Anthropic Messages and OpenAI Responses it has no effort chip and is sent no effort. When a provider rejects a request, the chat shows the provider's error.
- The language and decision model rows on `uw://providers` each have a Test button. It sends the model one small request and passes when the model answers in its format: text from a language model, a choice from a decision model. It checks that the model works, not how well it organizes; uw's own test suites check that. uw never tests a model on its own. The result shows in one line under the row, with Details. Agreed on 2026-10-15. Automatic organization runs while the settings switch is on and a model is set; it never waits for a test. A model that fails shows the error in the organizer's activity. Changed on 2026-10-16 from waiting for a passed test.
- A model list shows what the provider's own list says, minus names that are plainly not for chat, such as embedding, speech, and image models. The menu groups models by provider and has a search field.
- A chat whose model is no longer available says so on its chip and waits for the user to pick another. uw never switches a model on its own.
- On the first launch of the build that has this page, each saved connection becomes a provider. Connections with the same address and key become one. The organizer and chats go on with the same models, and the old preferences and Keychain accounts are removed once the move has succeeded.
- The organizer reads its two models from the providers. Nothing about how it organizes changes.
- Every provider's address can be changed, presets included, such as OpenAI at a company gateway. The Add dialog shows a preset's address filled in and editable, and a provider's page can change it later. A new address is saved only once uw lists its models there; the key and the model choices stay. A preset at another address keeps its icon, its format, and its catalog entries. Agreed on 2026-10-16.
- A [proxy](../CONTEXT.md#language), such as CLIProxyAPI, is added as Custom; there is no preset for one. Custom has a fourth format, "Each model's own": a model the catalog lists under Anthropic goes over Anthropic Messages, one under OpenAI over OpenAI Responses, and any other over chat completions, with the levels that catalog entry gives. uw works the format out when it lists the models and keeps it with the choice. Agreed on 2026-10-16.
- A Custom provider sends its key as `Authorization: Bearer`, and over Anthropic Messages also as `x-api-key`, so proxies that read either header work. The Anthropic preset keeps `x-api-key` alone. Agreed on 2026-10-16.
- An address is https, or plain http on this Mac or a private network: 10.x, 172.16–31.x, 192.168.x, `*.local`, and 100.64–127.x for Tailscale. Agreed on 2026-10-16.
- A network proxy needs nothing here: uw's model requests follow the Mac's proxy settings, like the rest of the browser.
- uw's settings section keeps automatic organization, the thresholds, pause, the tint, and the command bar switch, and links to `uw://providers`. The settings sidebar also lists "AI providers" under uw, with the external-link mark Extensions has; it opens `uw://providers` in a new tab. Agreed on 2026-10-16. The language, decision, and agent model cards leave it.

## Quick choices

Agreed on 2026-10-29; not built yet (T264 to T266). Found when the user added Vercel AI Gateway and OpenAI: the decision model menu offered none of Vercel's 14 decision models and all 51 of OpenAI's models, of which one can decide. Where these rules differ from the scope above, they win.

- **Rows by job.** The Models rows are named for what each model does: "Makes quick choices", "Names tasks", and "Starts chats". "Makes quick choices" is the [quick-choice model](../CONTEXT.md#language), described as "Where each new tab goes, and whether the agent's commands and memories are safe." "Names tasks" is the language model; "Starts chats" is the agent model. An empty choice reads "Same as Names tasks".
- **Decision models by provider.**

  | Provider | Its decision models | Requests go to |
  | --- | --- | --- |
  | Vercel AI Gateway | The models its list marks `"type": "evaluation"` | `<address>/decisions`, in the OpenAI Decisions format |
  | OpenRouter | Its list at `<address>/models?output_modalities=decisions` | `<address>/systemone`, in the System One format |
  | OpenAI | The models that answer a probe | `<address>/decisions`, as now |
  | Jev | Every model | `<address>/systemone`, as now |
  | Custom, in a decision format | The models that answer a probe | As now |

- **Probes**, per [ADR 0025](../adr/0025-uw-asks-a-provider-which-models-are-decision-models-when-its-list-does-not-say.md). Where a provider's list does not say, uw sends each listed model one decision request: the fixed sentence "uw checks that this model can answer." and one yes-or-no question. An answer makes it a decision model. An error saying the model cannot do decisions makes it not one. Any other failure, such as no network, a rejected key, a rate limit, or a server error, leaves it unknown until uw next lists the models.
  - uw probes when it lists a provider's models: when the provider is added, when the page opens, and when a model menu opens. It probes only models without a kept answer.
  - Answers are kept per provider and dropped when its address or key changes. A no is asked again after 7 days. A yes stays until the model leaves the provider's list.
- **While uw probes.** The quick choices menu shows a spinner and "Checking 51 models…" under the provider's name and fills in as answers come. A provider with none loses its heading. The provider row reads "51 models · checking", then "51 models · 1 decision", the count every provider with decision models shows. A provider's page tags each decision model "Decision model". A failed probe adds no message of its own: the row's status already shows a rejected key.
- **Menus.** The quick choices menu lists "Decision models" first, by provider, then "Or a language model": "Same as Names tasks", then every language model by provider, with search. With no decision model anywhere, it lists the language models and ends with "Decision models are faster and cheaper for this. Vercel AI Gateway, OpenRouter, OpenAI, and Jev have them." and a link to Add provider. The Names tasks, Starts chats, and chat composer menus leave out the models a provider's list marks as decision models. A model that also writes text, such as OpenAI's `gpt-6-luna`, shows in every menu.
- **A language model for quick choices.** Its chip carries a gray "Language model" tag. It gets the [classification call](../CONTEXT.md#language) in its own format with thinking off, as the command check does now. The Names tasks model always names new tasks: with a different model in each row, a new task costs one more small call; with the same model, it stays one call.
- **Test** passes when a decision model answers with a choice, or a language model with a placement uw can read.
- **Upgrade.** Saved choices stay. A saved decision model choice that is a language model runs as one, with its tag.

## Design

The "AI providers" page on the [Doop canvas](../../AGENTS.md#designs) holds the designs, and the `uw-arc-style` guide has their rules under "AI providers".

- "Providers page: models and providers (Fluid)": the three model choices and the provider list.
- "Providers page: a provider open (Fluid)": `uw://providers/<id>` with its address, key, format, and each model's tool calls and effort levels.
- "Providers page: add a provider (Fluid)": the preset tiles and a Custom provider. uw lists the address's models before it adds the provider.
- "Chat: model and effort chips (Fluid)": the composer's chips and the model menu.
- "AI providers: every state (Fluid)": effort levels, no effort levels, a model the catalog does not know, a model without tool calls, a missing model, a switch in the middle of a chat, the upgrade notice, provider problems, and the link from uw's settings section.

Effort levels show in lower case, as each provider names them. A thin line in the chat marks where the model or effort changed; it is open to review.

- **Page.** `uw://providers` is a second page in the React package of the chat page, `browser/resources/uw_chat/app`, with its own entry in `app/bundle.ts`, per [ADR 0006](../adr/0006-the-chat-page-is-react-with-fluid-functionalism.md). Its chips are the same components as the chat composer's.
- **Storage.** The providers live in profile preferences: an id, the preset, a name, the address, and the format. Changed on 2026-10-21: every profile shares one provider list, and each profile keeps its own model picks, per the [workspaces plan](workspaces.md), T130. Built that day: the shared list is the local state pref `uw.providers.shared`, and `ProviderSync` copies it into each loaded profile's own list and back on every change, so the code that reads a profile's providers stays as it is. The first time a profile loads, it adds the providers only it had, and local state notes it in `uw.providers.joined`. After that it only takes the shared list, so a provider removed while the profile was not loaded stays removed. Each key sits in Keychain under one account per provider. The model catalog is a file in the profile.
- **Formats.** Requests keep the shape of chat completions, which is what the organizer, chats, and suggestions already build. Each format's adapter in `components/ai_providers/` turns such a request into its own format and its answer back into chat completions' shape, so the code that reads answers stays as it is. The organizer's placement and naming requests go through the same adapters. What a provider sends that uw does not read, such as Anthropic's signed thinking or OpenAI's sealed reasoning, rides along on the answer, is saved with the chat, goes back only to the address that sent it, and is dropped when a chat switches provider. Requests over OpenAI Responses send `store: false`, so OpenAI keeps nothing. Changed on 2026-10-16 from adapters beside `components/agent/agent_protocol.h`, since the organizer uses them too.
- **Shared code.** `RemoteModelClient`, the Keychain store, and the model choices as requests need them move out of `browser/tab_organization/` to `browser/ai_providers/`, since chats and the organizer both use them. The organizer's own settings stay in `ai_connection.h`.
- **Icons.** Provider icons come from `@lobehub/icons-static-svg` (MIT, no dependencies), which has OpenAI, Anthropic, OpenRouter, Vercel, and Ollama. Jev and Custom get a plain icon.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T76: the design on the canvas: `uw://providers` with its states, adding a provider, and the model and effort chips with their menus. Signed off on 2026-10-15.
2. T77: providers and `uw://providers`: the provider list, keys, the three model choices, moving the saved connections, the organizer reading from providers, and the link from settings. Built on 2026-10-15.
3. T78: the Anthropic Messages and OpenAI Responses formats. Built on 2026-10-16, with the shared code moved to `browser/ai_providers/`. Live calls over each format are for the user to check: the tests play the providers with a local server.
4. T79: the model catalog, and the model and effort chips in the chat composer. Built on 2026-10-16. What a model kept goes back only to that model, so a switch to another model at the same provider drops it.
5. T103: proxies and addresses: changeable addresses, the "Each model's own" format, the key headers, and plain http on private networks. Built on 2026-10-16, on T104's catalog lookups by address. A live check through CLIProxyAPI is for the user: the tests play a proxy with a local server.
6. T264: the design of [quick choices](#quick-choices) on the canvas: the rows by job with each menu, and every state.
7. T265: decision models by provider: the lists, probes, the counts and tags, and decision models out of the language and agent menus. The tests play the providers; a live OpenRouter decision is for the user to check. Built on 2026-10-30: `DecisionProbes` keeps the answers in the profile pref `uw.providers.decision_probes`, and the page lists a provider again with each answer while it says "checking". Until T266 the rows keep their old names.
8. T266: the rows by job and a language model for quick choices, with the other plans, the architecture, and the Help page.

## Acceptance checks

1. With OpenAI added once, the language model, the agent model, and an OpenAI Decisions model all work from its one key.
2. After an upgrade, the organizer files tabs and chats run with no setup, and the old Keychain accounts are gone.
3. A chat switched from an Anthropic model to an OpenAI model in the middle of a request goes on, and its next request goes to OpenAI over Responses.
4. A model the catalog marks without tool calls is dimmed in a chat's model menu and cannot be picked there.
5. The effort chip lists the catalog's levels for the model. Picking a model sends no request to it.
6. A chat whose provider was removed says so and waits for the user to pick a model.
7. A model picked while automatic organization is on is used from the next tab on, without a test.
8. With CLIProxyAPI added as Custom with "Each model's own", a Claude model goes over Messages and a GPT model over Responses at `http://localhost:8317/v1`, each with the key as `Authorization: Bearer`.
9. Changing OpenAI's address to one that lists no models keeps the old address and says why.
10. A provider at `http://192.168.1.20:8317/v1` can be added; one at a public `http://` address cannot.
11. `pnpm run check` and the focused test suites pass. Browser tests stay headless.
12. With Vercel AI Gateway added, the quick choices menu lists its 14 decision models, and none of them is in the Names tasks, Starts chats, or chat composer menus.
13. With OpenAI added, uw probes its models once, and the quick choices menu lists `gpt-6-luna` under OpenAI. Listing them again sends no probe until a no is 7 days old.
14. With OpenRouter added, its decision models are listed, and one places a tab over `/systemone`.
15. With a language model making quick choices and another naming tasks, the first places a new tab and the second names its task. The chip says "Language model".
16. With only Anthropic added, the quick choices menu lists its language models, the hint, and Add provider.

## Open questions

- Whether the managed local model of [AI connections](../product.md#ai-connections) is added on this page.
