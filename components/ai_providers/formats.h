// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_
#define UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_

#include <optional>
#include <string>
#include <string_view>

#include "uw/components/ai_providers/providers.h"

namespace uw {

// uw builds every request to a language model in the shape of chat
// completions: "model", "messages", and maybe "tools" and "max_tokens". These
// turn such a request into the format a provider speaks, and its answer back
// into a chat-completions answer, so the code that reads answers stays the
// same. See ADR 0007.

// What a provider sent that uw does not read, such as Anthropic's signed
// thinking or OpenAI's sealed reasoning, rides on the assistant message under
// this key: {"address": ..., "format": ..., "items": [...]}.
inline constexpr char kKeptPartsKey[] = "uw_kept";

// The path under a provider's base address that takes language-model
// requests in `format`.
std::string_view LanguageModelPath(ProviderFormat format);

// Turns a chat-completions request into one in `format`. The parts an earlier
// answer kept go back only to `address`, the one that sent them, in the
// format they came in. Nothing for a body that is not such a request.
std::optional<std::string> RequestIn(ProviderFormat format,
                                     std::string_view chat_body,
                                     std::string_view address);

// Turns a successful answer in `format` into a chat-completions answer. What
// uw does not read is kept on the message, marked with `address`. Nothing for
// a body that is not such an answer.
std::optional<std::string> AnswerFrom(ProviderFormat format,
                                      std::string_view body,
                                      std::string_view address);

}  // namespace uw

#endif  // UW_COMPONENTS_AI_PROVIDERS_FORMATS_H_
