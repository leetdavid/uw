// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_
#define UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_

#include <optional>
#include <string>
#include <string_view>

#include "uw/components/ai_providers/catalog.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {

// uw builds every request to a language model in the shape of chat
// completions: "model", "messages", and maybe "tools" and "max_tokens". These
// turn such a request into the format a provider speaks, and its answer back
// into a chat-completions answer, so the code that reads answers stays the
// same. See ADR 0007.

// What a provider sent that uw does not read, such as Anthropic's signed
// thinking or OpenAI's sealed reasoning, rides on the assistant message under
// this key: {"address": ..., "model": ..., "format": ..., "items": [...]}.
inline constexpr char kKeptPartsKey[] = "uw_kept";

// Marks a user message uw adds to show the model an image it recalled. It is
// not a new turn from the user, so what the model kept still goes back.
inline constexpr char kShownKey[] = "uw_shown";

// The path under a provider's base address that takes language-model
// requests in `format`.
std::string_view LanguageModelPath(ProviderFormat format);

// Asks for `effort` the way `preset` takes it over chat completions:
// OpenRouter and Vercel's gateway nest it in "reasoning", others take
// "reasoning_effort". RequestIn turns the latter into Anthropic's and
// OpenAI's own fields. An empty `effort` leaves the request as it is.
// Asks the model `abilities` describes, at a provider made from `preset`, to
// answer over `format` without thinking: with effort "none" when the model
// takes it, else with the provider's own switch when the catalog says the
// model has one. That is thinking.type "disabled" over chat completions, as
// DeepSeek reads it, reasoning.enabled false at OpenRouter and Vercel's
// gateway, and reasoning_effort "none" at Ollama. Anthropic Messages thinks
// only when asked to, so its request stays as it is. Nothing when uw knows
// no way.
std::optional<std::string> WithoutThinking(std::string_view chat_body,
                                           ProviderPreset preset,
                                           ProviderFormat format,
                                           const ModelAbilities& abilities);

std::string WithEffort(std::string_view chat_body,
                       ProviderPreset preset,
                       std::string_view effort);

// Turns a chat-completions request into one in `format`. The parts an earlier
// answer kept go back only to the model that sent them, at `address`, in the
// format they came in. Nothing for a body that is not such a request.
std::optional<std::string> RequestIn(ProviderFormat format,
                                     std::string_view chat_body,
                                     std::string_view address);

// Turns a successful answer in `format` into a chat-completions answer, its
// token usage included. What uw does not read is kept on the message, marked
// with `address` and the `model` that was asked. Nothing for a body that is
// not such an answer.
std::optional<std::string> AnswerFrom(ProviderFormat format,
                                      std::string_view body,
                                      std::string_view address,
                                      std::string_view model);

}  // namespace uw

#endif  // UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_
