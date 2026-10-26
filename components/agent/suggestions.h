// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_SUGGESTIONS_H_
#define UW_COMPONENTS_AGENT_SUGGESTIONS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uw {

// How many requests a new chat suggests.
inline constexpr size_t kSuggestionCount = 3;
// The longest suggestion kept, in characters.
inline constexpr size_t kMaxSuggestionLength = 80;
// How many recent chats and most-visited sites the model is told about.
inline constexpr size_t kSuggestionChats = 20;
inline constexpr size_t kSuggestionSites = 15;

// A site the user visits often: its title, and its address without the query
// or fragment.
struct VisitedSite {
  std::string title;
  std::string address;
};

// Builds the chat-completions request that asks the language model for
// requests a new chat could suggest, from the titles of the user's recent
// chats and the sites they visited most this week. Nothing when `model` is
// empty or there is nothing to go on.
std::optional<std::string> BuildSuggestionsRequestBody(
    std::string_view model,
    const std::vector<std::string>& chat_titles,
    const std::vector<VisitedSite>& sites);

// The suggestions in a chat-completions response: a JSON array of strings,
// alone or in a Markdown code fence. Blank and overlong ones are dropped, and
// at most kSuggestionCount are kept. Nothing when none is usable.
std::optional<std::vector<std::string>> ParseSuggestionsResponse(
    std::string_view body);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_SUGGESTIONS_H_
