# uw asks a provider which models are decision models when its list does not say

Status: agreed on 2026-10-29. Not built yet.

A [decision model](../CONTEXT.md#language) menu should list only decision models. Most providers' model lists say which models those are: Vercel AI Gateway marks them `type: "evaluation"`, OpenRouter lists them with `?output_modalities=decisions`, and every Jev model is one. OpenAI's list does not say, and neither does the models.dev catalog: `gpt-6-luna` is listed as an ordinary language model, though it is the only model OpenAI's `/v1/decisions` takes. On 2026-10-29 the user asked for a long-term answer rather than a list kept in uw.

So where a provider's list does not say, uw sends each model one small decision request and keeps the answer. That covers the OpenAI preset and Custom providers in a decision format. A probe carries a fixed sentence and nothing about the user. The Decisions API bills only input, so a probe costs about $0.000002, and a model that refuses costs nothing.

This is an exception to "uw never sends a request just to test a model" in [ADR 0008](0008-uw-reads-what-a-model-can-do-from-models-dev.md). That rule still holds for what the catalog covers, such as tool calls and effort levels.

Considered options:

- A list of OpenAI's decision models kept in uw. The smallest change, but it needs a uw release each time OpenAI adds one.
- Every model of the provider, as before. 50 of OpenAI's 51 models fail.
- Reading Vercel's or OpenRouter's public list, where `openai/gpt-6-luna-decisions` stands for OpenAI's `gpt-6-luna`. No requests with the user's key, but it rests on a naming habit neither gateway promises, and it covers OpenAI only.
