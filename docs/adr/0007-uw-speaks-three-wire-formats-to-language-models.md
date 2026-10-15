# uw speaks three wire formats to language models

Status: agreed on 2026-10-15 for v2.1. Built on 2026-10-16 (T78).

uw first reached every language model over chat completions. Once the user can add any [provider](../CONTEXT.md#language) and pick its models, that one format is not enough: OpenAI's newest models refuse tools over chat completions while they reason, and Anthropic's compatible endpoint drops its thinking. The agent needs tool calls from every model it runs on.

So uw speaks three formats, each a small adapter over one plain shape of words, tool calls, and tool results:

- Chat completions, for Ollama, OpenRouter, Vercel AI Gateway, and custom addresses.
- Anthropic Messages, for Anthropic.
- OpenAI Responses, for OpenAI.

The plain shape is the request as chat completions takes it, which the organizer, chats, and suggestions already build; each adapter turns it into its format and the answer back. Decided on 2026-10-16, when T78 was built.

No vendor SDK and no model library. What a provider sends back that uw does not read, such as Anthropic's signed thinking or OpenAI's sealed reasoning, is kept whole and sent back on the next call, only to the model that sent it.

An [effort](../CONTEXT.md#language) level goes in the field each format reads: `reasoning_effort` over chat completions, or `reasoning.effort` at OpenRouter and Vercel AI Gateway, which read that instead; `reasoning.effort` over Responses; and adaptive thinking with `output_config.effort` over Messages. Until the user picks a level, uw sends none, and the provider's default applies. Decided on 2026-10-16, when T79 was started.

The two decision formats in [ADR 0003](0003-decision-model-beside-the-language-model.md) stay as they are.

Considered options: chat completions only, with Anthropic through its compatible endpoint (rejected: OpenAI's reasoning models lose tools, Anthropic loses thinking); chat completions plus Anthropic, adding Responses when an OpenAI model fails (rejected: the failure is already known, so waiting for it only delays the fix).
