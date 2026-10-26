// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/suggestions.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "uw/components/agent/agent_protocol.h"

namespace uw {
namespace {

// Room for a model that thinks before it answers.
constexpr int kMaxAnswerTokens = 4096;
// The longest chat title or site title sent, in bytes.
constexpr size_t kMaxTitleBytes = 200;

constexpr char kInstructions[] =
    "You suggest requests for a browser agent. The agent opens web pages in "
    "the user's browser, reads them, clicks, and types, on sites where the "
    "user is signed in. From the user's recent chats with the agent and the "
    "sites they visit most, write three requests the user is likely to want "
    "next. Write each one as the user would ask it, about their own sites, in "
    "under 60 characters. Do not repeat a recent chat. Answer with only a "
    "JSON array of three strings.";

std::string Shortened(std::string_view text) {
  std::string shortened;
  base::TruncateUTF8ToByteSize(std::string(text), kMaxTitleBytes, &shortened);
  return shortened;
}

// The text alone, out of a Markdown code fence if it is in one.
std::string_view Unfenced(std::string_view text) {
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  if (!text.starts_with("```")) {
    return text;
  }
  const size_t line = text.find('\n');
  const size_t end = text.rfind("```");
  if (line == std::string_view::npos || end <= line) {
    return std::string_view();
  }
  return text.substr(line + 1, end - line - 1);
}

}  // namespace

std::optional<std::string> BuildSuggestionsRequestBody(
    std::string_view model,
    const std::vector<std::string>& chat_titles,
    const std::vector<VisitedSite>& sites) {
  if (model.empty() || (chat_titles.empty() && sites.empty())) {
    return std::nullopt;
  }
  std::string text = "Recent chats:";
  if (chat_titles.empty()) {
    text += " none";
  }
  for (size_t i = 0; i < chat_titles.size() && i < kSuggestionChats; ++i) {
    text += "\n- " + Shortened(chat_titles[i]);
  }
  text += "\n\nSites visited most this week:";
  if (sites.empty()) {
    text += " none";
  }
  for (size_t i = 0; i < sites.size() && i < kSuggestionSites; ++i) {
    text += "\n- " + Shortened(sites[i].title) + " (" + sites[i].address + ")";
  }
  return base::WriteJson(
      base::DictValue()
          .Set("model", model)
          .Set("messages", base::ListValue()
                               .Append(base::DictValue()
                                           .Set("role", "system")
                                           .Set("content", kInstructions))
                               .Append(UserMessage(text)))
          .Set("max_tokens", kMaxAnswerTokens));
}

std::optional<std::vector<std::string>> ParseSuggestionsResponse(
    std::string_view body) {
  const std::optional<ModelTurn> turn = ParseAgentResponse(body);
  if (!turn) {
    return std::nullopt;
  }
  std::optional<base::Value> answer =
      base::JSONReader::Read(Unfenced(turn->text), base::JSON_PARSE_RFC);
  if (!answer || !answer->is_list()) {
    return std::nullopt;
  }
  std::vector<std::string> suggestions;
  for (const base::Value& item : answer->GetList()) {
    if (!item.is_string()) {
      continue;
    }
    std::string text(
        base::TrimWhitespaceASCII(item.GetString(), base::TRIM_ALL));
    if (text.empty() || base::UTF8ToUTF16(text).size() > kMaxSuggestionLength) {
      continue;
    }
    suggestions.push_back(std::move(text));
    if (suggestions.size() == kSuggestionCount) {
      break;
    }
  }
  if (suggestions.empty()) {
    return std::nullopt;
  }
  return suggestions;
}

}  // namespace uw
