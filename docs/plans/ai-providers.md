# AI providers

Status: scope agreed on 2026-10-15 for v2.1. Not built. The design comes first, in T76.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Two of the three formats are new to uw, the effort probe has never run against a real provider, it is a second React page, and the connections users already saved have to move without loss.

## Agreed scope

- A [provider](../CONTEXT.md#language) is saved once, with its key, on a new page, `uw://providers`. Every model choice in uw picks from these providers, so one key serves every feature that uses that provider.
- The Add provider menu names OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, and Jev, each with its address and format filled in. Custom takes any address, and the user picks its format. Other providers work through OpenRouter, the AI Gateway, or Custom.
- uw speaks three formats to language models: chat completions, Anthropic Messages, and OpenAI Responses. See [ADR 0007](../adr/0007-uw-speaks-three-wire-formats-to-language-models.md). The two decision formats of [ADR 0003](../adr/0003-decision-model-beside-the-language-model.md) stay.
- `uw://providers` holds the provider list and three model choices: the [language model](../CONTEXT.md#language), the [decision model](../CONTEXT.md#language) (optional), and the [agent model](../CONTEXT.md#language). Until an agent model is picked, chats use the language model.
- A model choice is a chip with the provider's icon and the model's name. Beside it, an [effort](../CONTEXT.md#language) chip lists that provider's own levels for that model. The language model and the agent model get one; decision models have no effort setting.
- The chat composer has the same two chips. The user can switch either one at any time, even in the middle of a chat, and the next request uses the new choice. Each saved chat keeps its own.
- The [model check](../CONTEXT.md#language) runs the first time a model is picked for chats. The model must call a tool. uw then sends a tiny request at each of the format's effort levels and keeps the ones the model accepts. Anthropic and OpenAI reject levels a model does not take; OpenRouter's model list says which it takes. Other chat-completions providers may ignore the setting silently, so their models get no effort chip unless the user turns one on for that model. A model that fails the check cannot run a chat, and its chip says why.
- The check's result is saved in the profile. It runs again only on "Check again", or when the provider's address or key changes. The language and decision models keep the connection tests they have now.
- A model list shows what the provider's own list says, minus names that are plainly not for chat, such as embedding, speech, and image models. The menu groups models by provider and has a search field.
- A chat whose model is no longer available says so on its chip and waits for the user to pick another. uw never switches a model on its own.
- On the first launch of the build that has this page, each saved connection becomes a provider. Connections with the same address and key become one. The organizer and chats go on with the same models, and the old preferences and Keychain accounts are removed once the move has succeeded.
- The organizer reads its two models from the providers. Nothing about how it organizes changes.
- uw's settings section keeps automatic organization, the thresholds, pause, the tint, and the command bar switch, and links to `uw://providers`. The language, decision, and agent model cards leave it.

## Design

The "AI providers" page on the [Doop canvas](../../AGENTS.md#designs) holds the designs. They are proposals until the user signs off in T76.

- **Page.** `uw://providers` is a second page in the React package of the chat page, `browser/resources/uw_chat/app`, with its own entry in `app/bundle.ts`, per [ADR 0006](../adr/0006-the-chat-page-is-react-with-fluid-functionalism.md). Its chips are the same components as the chat composer's.
- **Storage.** The providers live in profile preferences: an id, the preset, a name, the address, and the format. Each key sits in Keychain under one account per provider. Model check results are profile preferences too.
- **Formats.** One plain shape of words, tool calls, and tool results goes in; each format's adapter, beside `components/agent/agent_protocol.h`, turns it into that format and back. What a provider sends that uw does not read, such as Anthropic's signed thinking, is sent back only to the provider that sent it, and is dropped when a chat switches provider.
- **Shared code.** `RemoteModelClient`, the Keychain store, and the connection preferences move out of `browser/tab_organization/` to a shared folder, since chats and the organizer both use them.
- **Icons.** Provider icons come from `@lobehub/icons-static-svg` (MIT, no dependencies), which has OpenAI, Anthropic, OpenRouter, Vercel, and Ollama. Jev and Custom get a plain icon.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T76: the design on the canvas: `uw://providers` with its states, adding a provider, the model and effort chips with their menus, and the model check. The user signs off before T77.
2. T77: providers and `uw://providers`: the provider list, keys, the three model choices, moving the saved connections, the organizer reading from providers, and the link from settings.
3. T78: the Anthropic Messages and OpenAI Responses formats.
4. T79: the model check with the effort probe, and the model and effort chips in the chat composer.

## Acceptance checks

1. With OpenAI added once, the language model, the agent model, and an OpenAI Decisions model all work from its one key.
2. After an upgrade, the organizer files tabs and chats run with no setup, and the old Keychain accounts are gone.
3. A chat switched from an Anthropic model to an OpenAI model in the middle of a request goes on, and its next request goes to OpenAI over Responses.
4. A model that never calls a tool cannot run a chat, and its chip says why.
5. The effort chip lists the levels the probe found. Picking the same model again sends no probe.
6. A chat whose provider was removed says so and waits for the user to pick a model.
7. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

- Whether the managed local model of [AI connections](../product.md#ai-connections) is added on this page.
