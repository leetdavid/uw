# AI providers

Status: scope agreed on 2026-10-15 for v2.1. The user signed off on the design the same day, with the model check replaced by the [model catalog](../CONTEXT.md#language). Not built.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Two of the three formats are new to uw, it is a second React page, and the connections users already saved have to move without loss.

## Agreed scope

- A [provider](../CONTEXT.md#language) is saved once, with its key, on a new page, `uw://providers`. Every model choice in uw picks from these providers, so one key serves every feature that uses that provider.
- The Add provider menu names OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, and Jev, each with its address and format filled in. Custom takes any address, and the user picks its format. Other providers work through OpenRouter, the AI Gateway, or Custom.
- uw speaks three formats to language models: chat completions, Anthropic Messages, and OpenAI Responses. See [ADR 0007](../adr/0007-uw-speaks-three-wire-formats-to-language-models.md). The two decision formats of [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md) stay.
- `uw://providers` holds the provider list and three model choices: the [language model](../CONTEXT.md#language), the [decision model](../CONTEXT.md#language) (optional), and the [agent model](../CONTEXT.md#language). Until an agent model is picked, chats use the language model.
- A model choice is a chip with the provider's icon and the model's name. Beside it, an [effort](../CONTEXT.md#language) chip lists that model's levels, named as the provider names them. The language model and the agent model get one; decision models and models without effort levels have none. A model the catalog gives only an on/off switch or a token budget, such as claude-sonnet-4-5, counts as having no levels and runs without thinking. Agreed on 2026-10-15: these are older models, so uw does not add a chip for them.
- The chat composer has the same two chips. The user can switch either one at any time, even in the middle of a chat, and the next request uses the new choice. Each saved chat keeps its own.
- What a model can do comes from the model catalog, per [ADR 0008](../adr/0008-uw-reads-what-a-model-can-do-from-models-dev.md): whether it calls tools, and its effort levels. uw never sends a request just to test a model. Changed on 2026-10-15 from a test on first use, to follow OpenCode.
- A model the catalog marks without tool calls shows dimmed in a chat's model menu and cannot run a chat. For Ollama, which the catalog does not cover, its own model details say whether a model calls tools and thinks. A model the catalog does not know is taken to call tools. Over chat completions it gets `low`, `medium`, and `high`; over Anthropic Messages and OpenAI Responses it has no effort chip and is sent no effort. When a provider rejects a request, the chat shows the provider's error.
- The language and decision model rows on `uw://providers` each have a Test button. It sends the model one small request and passes when the model answers in its format: text from a language model, a choice from a decision model. It checks that the model works, not how well it organizes; uw's own test suites check that. uw never tests a model on its own. The result shows in one line under the row, with Details. Automatic organization runs while the settings switch is on and every set model passed its last test; a newly picked model waits for its test, and the switch says so. Agreed on 2026-10-15.
- A model list shows what the provider's own list says, minus names that are plainly not for chat, such as embedding, speech, and image models. The menu groups models by provider and has a search field.
- A chat whose model is no longer available says so on its chip and waits for the user to pick another. uw never switches a model on its own.
- On the first launch of the build that has this page, each saved connection becomes a provider. Connections with the same address and key become one. The organizer and chats go on with the same models, and the old preferences and Keychain accounts are removed once the move has succeeded.
- The organizer reads its two models from the providers. Nothing about how it organizes changes.
- uw's settings section keeps automatic organization, the thresholds, pause, the tint, and the command bar switch, and links to `uw://providers`. The language, decision, and agent model cards leave it.

## Design

The "AI providers" page on the [Doop canvas](../../AGENTS.md#designs) holds the designs, and the `uw-arc-style` guide has their rules under "AI providers".

- "Providers page: models and providers (Fluid)": the three model choices and the provider list.
- "Providers page: a provider open (Fluid)": `uw://providers/<id>` with its address, key, format, and each model's tool calls and effort levels.
- "Providers page: add a provider (Fluid)": the preset tiles and a Custom provider. uw lists the address's models before it adds the provider.
- "Chat: model and effort chips (Fluid)": the composer's chips and the model menu.
- "AI providers: every state (Fluid)": effort levels, no effort levels, a model the catalog does not know, a model without tool calls, a missing model, a switch in the middle of a chat, the upgrade notice, provider problems, and the link from uw's settings section.

Effort levels show in lower case, as each provider names them. A thin line in the chat marks where the model or effort changed; it is open to review.

- **Page.** `uw://providers` is a second page in the React package of the chat page, `browser/resources/uw_chat/app`, with its own entry in `app/bundle.ts`, per [ADR 0006](../adr/0006-the-chat-page-is-react-with-fluid-functionalism.md). Its chips are the same components as the chat composer's.
- **Storage.** The providers live in profile preferences: an id, the preset, a name, the address, and the format. Each key sits in Keychain under one account per provider. The model catalog is a file in the profile.
- **Formats.** One plain shape of words, tool calls, and tool results goes in; each format's adapter, beside `components/agent/agent_protocol.h`, turns it into that format and back. What a provider sends that uw does not read, such as Anthropic's signed thinking, is sent back only to the provider that sent it, and is dropped when a chat switches provider.
- **Shared code.** `RemoteModelClient`, the Keychain store, and the connection preferences move out of `browser/tab_organization/` to a shared folder, since chats and the organizer both use them.
- **Icons.** Provider icons come from `@lobehub/icons-static-svg` (MIT, no dependencies), which has OpenAI, Anthropic, OpenRouter, Vercel, and Ollama. Jev and Custom get a plain icon.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T76: the design on the canvas: `uw://providers` with its states, adding a provider, and the model and effort chips with their menus. Signed off on 2026-10-15.
2. T77: providers and `uw://providers`: the provider list, keys, the three model choices, moving the saved connections, the organizer reading from providers, and the link from settings.
3. T78: the Anthropic Messages and OpenAI Responses formats.
4. T79: the model catalog, and the model and effort chips in the chat composer.

## Acceptance checks

1. With OpenAI added once, the language model, the agent model, and an OpenAI Decisions model all work from its one key.
2. After an upgrade, the organizer files tabs and chats run with no setup, and the old Keychain accounts are gone.
3. A chat switched from an Anthropic model to an OpenAI model in the middle of a request goes on, and its next request goes to OpenAI over Responses.
4. A model the catalog marks without tool calls is dimmed in a chat's model menu and cannot be picked there.
5. The effort chip lists the catalog's levels for the model. Picking a model sends no request to it.
6. A chat whose provider was removed says so and waits for the user to pick a model.
7. Picking a different language model stops automatic organization until the user tests it and the test passes.
8. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

- Whether the managed local model of [AI connections](../product.md#ai-connections) is added on this page.
