// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/formats.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace uw {
namespace {

// Anthropic needs a limit on every request; chats set none. Thinking counts
// against it.
constexpr int kAnthropicMaxTokens = 16000;

std::string_view StringOr(const base::DictValue& dict,
                          std::string_view key,
                          std::string_view fallback = "") {
  const std::string* value = dict.FindString(key);
  return value ? std::string_view(*value) : fallback;
}

// The parts of a user message: its text, or the list it has when it carries
// images, as chat completions writes them.
base::ListValue UserParts(const base::DictValue& message) {
  if (const base::ListValue* parts = message.FindList("content")) {
    return parts->Clone();
  }
  base::ListValue parts;
  if (const std::string* text = message.FindString("content");
      text && !text->empty()) {
    parts.Append(base::DictValue().Set("type", "text").Set("text", *text));
  }
  return parts;
}

// The address of an image part, such as a data: URL.
std::string_view ImageUrl(const base::DictValue& part) {
  const base::DictValue* image = part.FindDict("image_url");
  return image ? StringOr(*image, "url") : "";
}

// The kept parts of `message`, when `model` at `address` sent them in
// `format`. Another model, even at the same provider, may refuse them.
const base::ListValue* KeptParts(const base::DictValue& message,
                                 ProviderFormat format,
                                 std::string_view address,
                                 std::string_view model) {
  const base::DictValue* kept = message.FindDict(kKeptPartsKey);
  if (!kept || StringOr(*kept, "address") != address ||
      StringOr(*kept, "model") != model ||
      kept->FindInt("format") != static_cast<int>(format)) {
    return nullptr;
  }
  return kept->FindList("items");
}

base::DictValue KeptPartsFrom(ProviderFormat format,
                              std::string_view address,
                              std::string_view model,
                              base::ListValue items) {
  return base::DictValue()
      .Set("address", address)
      .Set("model", model)
      .Set("format", static_cast<int>(format))
      .Set("items", std::move(items));
}

// The system messages, joined.
std::string Instructions(const base::ListValue& messages) {
  std::vector<std::string_view> parts;
  for (const base::Value& message : messages) {
    const base::DictValue* dict = message.GetIfDict();
    if (dict && StringOr(*dict, "role") == "system") {
      parts.push_back(StringOr(*dict, "content"));
    }
  }
  return base::JoinString(parts, "\n\n");
}

base::DictValue ArgumentsOf(const base::DictValue& call) {
  const base::DictValue* function = call.FindDict("function");
  std::optional<base::DictValue> parsed =
      function ? base::JSONReader::ReadDict(StringOr(*function, "arguments"),
                                            base::JSON_PARSE_RFC)
               : std::nullopt;
  return parsed ? std::move(*parsed) : base::DictValue();
}

std::optional<std::string> Write(const base::DictValue& dict) {
  return base::WriteJson(dict);
}

base::DictValue ChatAnswer(std::string text,
                           base::ListValue calls,
                           base::DictValue kept,
                           std::string_view finish_reason) {
  base::DictValue message = base::DictValue()
                                .Set("role", "assistant")
                                .Set("content", std::move(text));
  if (!calls.empty()) {
    message.Set("tool_calls", std::move(calls));
  }
  if (!kept.empty()) {
    message.Set(kKeptPartsKey, std::move(kept));
  }
  return base::DictValue().Set(
      "choices",
      base::ListValue().Append(base::DictValue()
                                   .Set("finish_reason", finish_reason)
                                   .Set("message", std::move(message))));
}

base::DictValue ToolCall(std::string_view id,
                         std::string_view name,
                         std::string arguments) {
  return base::DictValue()
      .Set("id", id)
      .Set("type", "function")
      .Set("function", base::DictValue()
                           .Set("name", name)
                           .Set("arguments", std::move(arguments)));
}

// Anthropic Messages: the system text apart, alternating user and assistant
// messages of content blocks, tool results as user blocks.
base::DictValue AnthropicRequest(const base::DictValue& chat,
                                 std::string_view address) {
  const base::ListValue empty;
  const base::ListValue* messages = chat.FindList("messages");
  base::ListValue out;
  const auto add = [&out](std::string_view role, base::ListValue blocks) {
    if (blocks.empty()) {
      return;
    }
    base::DictValue* last = out.empty() ? nullptr : out.back().GetIfDict();
    if (last && StringOr(*last, "role") == role) {
      for (base::Value& block : blocks) {
        last->FindList("content")->Append(std::move(block));
      }
      return;
    }
    out.Append(
        base::DictValue().Set("role", role).Set("content", std::move(blocks)));
  };
  const base::ListValue& all = messages ? *messages : empty;
  for (const base::Value& value : all) {
    const base::DictValue* message = value.GetIfDict();
    if (!message) {
      continue;
    }
    const std::string_view role = StringOr(*message, "role");
    const std::string_view text = StringOr(*message, "content");
    base::ListValue blocks;
    if (role == "user") {
      for (const base::Value& entry : UserParts(*message)) {
        const base::DictValue* part = entry.GetIfDict();
        if (!part) {
          continue;
        }
        if (StringOr(*part, "type") == "text") {
          blocks.Append(base::DictValue()
                            .Set("type", "text")
                            .Set("text", StringOr(*part, "text")));
          continue;
        }
        // data:<media type>;base64,<data>
        const std::string_view url = ImageUrl(*part);
        const size_t comma = url.find(",");
        if (url.starts_with("data:") && comma != std::string_view::npos &&
            url.substr(0, comma).ends_with(";base64")) {
          blocks.Append(
              base::DictValue()
                  .Set("type", "image")
                  .Set("source",
                       base::DictValue()
                           .Set("type", "base64")
                           .Set("media_type", url.substr(5, comma - 5 - 7))
                           .Set("data", url.substr(comma + 1))));
        }
      }
      add("user", std::move(blocks));
    } else if (role == "assistant") {
      if (const base::ListValue* kept =
              KeptParts(*message, ProviderFormat::kAnthropicMessages, address,
                        StringOr(chat, "model"))) {
        blocks = kept->Clone();
      }
      if (!text.empty()) {
        blocks.Append(base::DictValue().Set("type", "text").Set("text", text));
      }
      if (const base::ListValue* calls = message->FindList("tool_calls")) {
        for (const base::Value& call : *calls) {
          if (const base::DictValue* dict = call.GetIfDict()) {
            const base::DictValue* function = dict->FindDict("function");
            blocks.Append(
                base::DictValue()
                    .Set("type", "tool_use")
                    .Set("id", StringOr(*dict, "id"))
                    .Set("name", function ? StringOr(*function, "name") : "")
                    .Set("input", ArgumentsOf(*dict)));
          }
        }
      }
      add("assistant", std::move(blocks));
    } else if (role == "tool") {
      blocks.Append(base::DictValue()
                        .Set("type", "tool_result")
                        .Set("tool_use_id", StringOr(*message, "tool_call_id"))
                        .Set("content", text));
      add("user", std::move(blocks));
    }
  }
  base::DictValue request =
      base::DictValue()
          .Set("model", StringOr(chat, "model"))
          .Set("max_tokens",
               chat.FindInt("max_tokens").value_or(kAnthropicMaxTokens))
          .Set("messages", std::move(out));
  // Anthropic caches only what a request marks. The tools and instructions
  // are the same in every chat, so they stay for an hour.
  if (std::string system = Instructions(messages ? *messages : empty);
      !system.empty()) {
    request.Set("system",
                base::ListValue().Append(
                    base::DictValue()
                        .Set("type", "text")
                        .Set("text", std::move(system))
                        .Set("cache_control", base::DictValue()
                                                  .Set("type", "ephemeral")
                                                  .Set("ttl", "1h"))));
  }
  // An effort level asks the model to think as much as it says.
  if (const std::string* effort = chat.FindString("reasoning_effort")) {
    request.Set("thinking", base::DictValue().Set("type", "adaptive"));
    request.Set("output_config", base::DictValue().Set("effort", *effort));
  }
  if (const base::ListValue* tools = chat.FindList("tools")) {
    base::ListValue converted;
    for (const base::Value& tool : *tools) {
      const base::DictValue* function =
          tool.is_dict() ? tool.GetDict().FindDict("function") : nullptr;
      if (!function) {
        continue;
      }
      const base::DictValue* parameters = function->FindDict("parameters");
      converted.Append(
          base::DictValue()
              .Set("name", StringOr(*function, "name"))
              .Set("description", StringOr(*function, "description"))
              .Set("input_schema",
                   parameters ? parameters->Clone()
                              : base::DictValue().Set("type", "object")));
    }
    request.Set("tools", std::move(converted));
    // A chat with tools is the agent's, whose next request repeats this one,
    // so all of it is cached for a few minutes.
    request.Set("cache_control", base::DictValue().Set("type", "ephemeral"));
  }
  return request;
}

// OpenAI Responses: the system text as instructions, then a list of input
// items. Nothing is stored at OpenAI, so sealed reasoning comes back with the
// answer and goes along with the next request.
base::DictValue ResponsesRequest(const base::DictValue& chat,
                                 std::string_view address) {
  const base::ListValue empty;
  const base::ListValue* messages = chat.FindList("messages");
  base::ListValue input;
  const base::ListValue& all = messages ? *messages : empty;
  for (const base::Value& value : all) {
    const base::DictValue* message = value.GetIfDict();
    if (!message) {
      continue;
    }
    const std::string_view role = StringOr(*message, "role");
    const std::string_view text = StringOr(*message, "content");
    if (role == "user" && !message->FindList("content")) {
      input.Append(base::DictValue().Set("role", "user").Set("content", text));
    } else if (role == "user") {
      base::ListValue content;
      for (const base::Value& entry : UserParts(*message)) {
        const base::DictValue* part = entry.GetIfDict();
        if (!part) {
          continue;
        }
        if (StringOr(*part, "type") == "text") {
          content.Append(base::DictValue()
                             .Set("type", "input_text")
                             .Set("text", StringOr(*part, "text")));
        } else {
          content.Append(base::DictValue()
                             .Set("type", "input_image")
                             .Set("image_url", ImageUrl(*part)));
        }
      }
      input.Append(base::DictValue()
                       .Set("role", "user")
                       .Set("content", std::move(content)));
    } else if (role == "assistant") {
      if (const base::ListValue* kept =
              KeptParts(*message, ProviderFormat::kOpenAiResponses, address,
                        StringOr(chat, "model"))) {
        for (const base::Value& item : *kept) {
          input.Append(item.Clone());
        }
      }
      if (!text.empty()) {
        input.Append(
            base::DictValue().Set("role", "assistant").Set("content", text));
      }
      if (const base::ListValue* calls = message->FindList("tool_calls")) {
        for (const base::Value& call : *calls) {
          const base::DictValue* dict = call.GetIfDict();
          const base::DictValue* function =
              dict ? dict->FindDict("function") : nullptr;
          if (function) {
            input.Append(
                base::DictValue()
                    .Set("type", "function_call")
                    .Set("call_id", StringOr(*dict, "id"))
                    .Set("name", StringOr(*function, "name"))
                    .Set("arguments", StringOr(*function, "arguments", "{}")));
          }
        }
      }
    } else if (role == "tool") {
      input.Append(base::DictValue()
                       .Set("type", "function_call_output")
                       .Set("call_id", StringOr(*message, "tool_call_id"))
                       .Set("output", text));
    }
  }
  base::DictValue request =
      base::DictValue()
          .Set("model", StringOr(chat, "model"))
          .Set("store", false)
          .Set("include",
               base::ListValue().Append("reasoning.encrypted_content"))
          .Set("input", std::move(input));
  if (std::string instructions = Instructions(messages ? *messages : empty);
      !instructions.empty()) {
    request.Set("instructions", std::move(instructions));
  }
  if (std::optional<int> limit = chat.FindInt("max_tokens")) {
    request.Set("max_output_tokens", *limit);
  }
  if (const std::string* effort = chat.FindString("reasoning_effort")) {
    request.Set("reasoning", base::DictValue().Set("effort", *effort));
  }
  if (const base::ListValue* tools = chat.FindList("tools")) {
    base::ListValue converted;
    for (const base::Value& tool : *tools) {
      const base::DictValue* function =
          tool.is_dict() ? tool.GetDict().FindDict("function") : nullptr;
      if (!function) {
        continue;
      }
      base::DictValue entry = function->Clone();
      entry.Set("type", "function");
      converted.Append(std::move(entry));
    }
    request.Set("tools", std::move(converted));
  }
  return request;
}

std::optional<base::DictValue> AnthropicAnswer(const base::DictValue& answer,
                                               std::string_view address,
                                               std::string_view model) {
  const base::ListValue* content = answer.FindList("content");
  if (!content) {
    return std::nullopt;
  }
  std::string text;
  base::ListValue calls;
  base::ListValue kept;
  for (const base::Value& entry : *content) {
    const base::DictValue* block = entry.GetIfDict();
    if (!block) {
      continue;
    }
    const std::string_view type = StringOr(*block, "type");
    if (type == "text") {
      text += StringOr(*block, "text");
    } else if (type == "tool_use") {
      const base::DictValue* input = block->FindDict("input");
      calls.Append(
          ToolCall(StringOr(*block, "id"), StringOr(*block, "name"),
                   input ? base::WriteJson(*input).value_or("{}") : "{}"));
    } else if (type == "thinking" || type == "redacted_thinking") {
      kept.Append(block->Clone());
    }
  }
  const std::string_view stop = StringOr(answer, "stop_reason");
  return ChatAnswer(std::move(text), std::move(calls),
                    kept.empty()
                        ? base::DictValue()
                        : KeptPartsFrom(ProviderFormat::kAnthropicMessages,
                                        address, model, std::move(kept)),
                    stop == "tool_use"     ? "tool_calls"
                    : stop == "max_tokens" ? "length"
                                           : "stop");
}

std::optional<base::DictValue> ResponsesAnswer(const base::DictValue& answer,
                                               std::string_view address,
                                               std::string_view model) {
  const base::ListValue* output = answer.FindList("output");
  if (!output) {
    return std::nullopt;
  }
  std::string text;
  base::ListValue calls;
  base::ListValue kept;
  for (const base::Value& entry : *output) {
    const base::DictValue* item = entry.GetIfDict();
    if (!item) {
      continue;
    }
    const std::string_view type = StringOr(*item, "type");
    if (type == "message") {
      if (const base::ListValue* parts = item->FindList("content")) {
        for (const base::Value& part : *parts) {
          if (part.is_dict() &&
              StringOr(part.GetDict(), "type") == "output_text") {
            text += StringOr(part.GetDict(), "text");
          }
        }
      }
    } else if (type == "function_call") {
      calls.Append(ToolCall(StringOr(*item, "call_id"), StringOr(*item, "name"),
                            std::string(StringOr(*item, "arguments", "{}"))));
    } else if (type == "reasoning") {
      kept.Append(item->Clone());
    }
  }
  const bool cut_short = StringOr(answer, "status") == "incomplete";
  const bool calls_tools = !calls.empty();
  return ChatAnswer(std::move(text), std::move(calls),
                    kept.empty()
                        ? base::DictValue()
                        : KeptPartsFrom(ProviderFormat::kOpenAiResponses,
                                        address, model, std::move(kept)),
                    cut_short     ? "length"
                    : calls_tools ? "tool_calls"
                                  : "stop");
}

// A count in a provider's usage, which JSON may give as a decimal number.
int CountIn(const base::DictValue* usage, std::string_view path) {
  const base::Value* value = usage ? usage->FindByDottedPath(path) : nullptr;
  return value && (value->is_int() || value->is_double())
             ? static_cast<int>(value->GetDouble())
             : 0;
}

// An Anthropic or Responses answer's usage, as chat completions report it:
// every input token in prompt_tokens, with the cached ones and, for
// Anthropic, those written to its cache, among them.
std::optional<base::DictValue> ChatUsage(ProviderFormat format,
                                         const base::DictValue& answer) {
  const base::DictValue* usage = answer.FindDict("usage");
  if (!usage) {
    return std::nullopt;
  }
  if (format == ProviderFormat::kAnthropicMessages) {
    const int read = CountIn(usage, "cache_read_input_tokens");
    const int written = CountIn(usage, "cache_creation_input_tokens");
    return base::DictValue()
        .Set("prompt_tokens", CountIn(usage, "input_tokens") + read + written)
        .Set("completion_tokens", CountIn(usage, "output_tokens"))
        .Set("prompt_tokens_details", base::DictValue()
                                          .Set("cached_tokens", read)
                                          .Set("cache_write_tokens", written));
  }
  return base::DictValue()
      .Set("prompt_tokens", CountIn(usage, "input_tokens"))
      .Set("completion_tokens", CountIn(usage, "output_tokens"))
      .Set("prompt_tokens_details",
           base::DictValue().Set(
               "cached_tokens",
               CountIn(usage, "input_tokens_details.cached_tokens")))
      .Set("completion_tokens_details",
           base::DictValue().Set(
               "reasoning_tokens",
               CountIn(usage, "output_tokens_details.reasoning_tokens")));
}

}  // namespace

std::string_view LanguageModelPath(ProviderFormat format) {
  switch (format) {
    case ProviderFormat::kAnthropicMessages:
      return "/messages";
    case ProviderFormat::kOpenAiResponses:
      return "/responses";
    case ProviderFormat::kChatCompletions:
    case ProviderFormat::kOpenAiDecisions:
    case ProviderFormat::kSystemOne:
    case ProviderFormat::kEachModel:
      return "/chat/completions";
  }
}

std::string WithEffort(std::string_view chat_body,
                       ProviderPreset preset,
                       std::string_view effort) {
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(chat_body, base::JSON_PARSE_RFC);
  if (effort.empty() || !chat) {
    return std::string(chat_body);
  }
  if (preset == ProviderPreset::kOpenRouter ||
      preset == ProviderPreset::kVercelAiGateway) {
    chat->Set("reasoning", base::DictValue().Set("effort", effort));
  } else {
    chat->Set("reasoning_effort", effort);
  }
  return Write(*chat).value_or(std::string(chat_body));
}

std::optional<std::string> WithoutThinking(std::string_view chat_body,
                                           ProviderPreset preset,
                                           ProviderFormat format,
                                           const ModelAbilities& abilities) {
  if (format == ProviderFormat::kAnthropicMessages) {
    return std::string(chat_body);
  }
  if (std::ranges::contains(abilities.efforts, "none")) {
    return WithEffort(chat_body, preset, "none");
  }
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(chat_body, base::JSON_PARSE_RFC);
  if (!abilities.toggles_thinking || !chat) {
    return std::nullopt;
  }
  if (preset == ProviderPreset::kOpenRouter ||
      preset == ProviderPreset::kVercelAiGateway) {
    chat->Set("reasoning", base::DictValue().Set("enabled", false));
  } else if (preset == ProviderPreset::kOllama) {
    chat->Set("reasoning_effort", "none");
  } else if (format == ProviderFormat::kChatCompletions) {
    chat->Set("thinking", base::DictValue().Set("type", "disabled"));
  } else {
    return std::nullopt;
  }
  return Write(*chat);
}

std::optional<std::string> RequestIn(ProviderFormat format,
                                     std::string_view chat_body,
                                     std::string_view address) {
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(chat_body, base::JSON_PARSE_RFC);
  if (!chat || !chat->FindList("messages")) {
    return std::nullopt;
  }
  switch (format) {
    case ProviderFormat::kAnthropicMessages:
      return Write(AnthropicRequest(*chat, address));
    case ProviderFormat::kOpenAiResponses:
      return Write(ResponsesRequest(*chat, address));
    case ProviderFormat::kChatCompletions:
    case ProviderFormat::kOpenAiDecisions:
    case ProviderFormat::kSystemOne:
    case ProviderFormat::kEachModel:
      // Chat completions takes the request as it is, without what other
      // formats kept.
      for (base::Value& message : *chat->FindList("messages")) {
        if (message.is_dict()) {
          message.GetDict().Remove(kKeptPartsKey);
          message.GetDict().Remove(kShownKey);
        }
      }
      return Write(*chat);
  }
}

std::optional<std::string> AnswerFrom(ProviderFormat format,
                                      std::string_view body,
                                      std::string_view address,
                                      std::string_view model) {
  if (format != ProviderFormat::kAnthropicMessages &&
      format != ProviderFormat::kOpenAiResponses) {
    return std::string(body);
  }
  std::optional<base::DictValue> answer =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!answer) {
    return std::nullopt;
  }
  std::optional<base::DictValue> chat =
      format == ProviderFormat::kAnthropicMessages
          ? AnthropicAnswer(*answer, address, model)
          : ResponsesAnswer(*answer, address, model);
  if (!chat) {
    return std::nullopt;
  }
  if (std::optional<base::DictValue> usage = ChatUsage(format, *answer)) {
    chat->Set("usage", std::move(*usage));
  }
  return Write(*chat);
}

}  // namespace uw
