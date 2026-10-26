// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/suggestions.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

// A chat-completions response whose answer is `content`.
std::string Answer(std::string_view content) {
  return *base::WriteJson(base::DictValue().Set(
      "choices", base::ListValue().Append(base::DictValue().Set(
                     "message", base::DictValue()
                                    .Set("role", "assistant")
                                    .Set("content", content)))));
}

// The text of the request's user message.
std::string UserText(const std::string& body) {
  std::optional<base::DictValue> request =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  const base::ListValue* messages = request->FindList("messages");
  return *messages->back().GetDict().FindString("content");
}

TEST(SuggestionsTest, TheRequestListsRecentChatsAndSites) {
  const std::optional<std::string> body = BuildSuggestionsRequestBody(
      "deepseek-flash", {"Did the Vercel deploy pass?", "Renew my passport"},
      {{"Dashboard – Vercel", "vercel.com/dashboard"},
       {"Inbox (3)", "mail.google.com/mail/u/0"}});
  ASSERT_TRUE(body);
  std::optional<base::DictValue> request =
      base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("model"), "deepseek-flash");
  EXPECT_EQ(request->FindList("messages")->size(), 2u);
  EXPECT_EQ(UserText(*body),
            "Recent chats:\n"
            "- Did the Vercel deploy pass?\n"
            "- Renew my passport\n"
            "\n"
            "Sites visited most this week:\n"
            "- Dashboard – Vercel (vercel.com/dashboard)\n"
            "- Inbox (3) (mail.google.com/mail/u/0)");
}

TEST(SuggestionsTest, TheRequestSaysWhenThereAreNoChats) {
  const std::optional<std::string> body = BuildSuggestionsRequestBody(
      "m", {}, {{"Railway", "railway.com/dashboard"}});
  ASSERT_TRUE(body);
  EXPECT_THAT(UserText(*body), HasSubstr("Recent chats: none"));
}

TEST(SuggestionsTest, TheRequestKeepsToTheLimits) {
  std::vector<std::string> chats(30, std::string(500, 'a'));
  std::vector<VisitedSite> sites(30, VisitedSite{"Site", "site.test"});
  const std::optional<std::string> body =
      BuildSuggestionsRequestBody("m", chats, sites);
  ASSERT_TRUE(body);
  const std::string text = UserText(*body);
  EXPECT_THAT(text, HasSubstr("- " + std::string(200, 'a') + "\n"));
  EXPECT_THAT(text, ::testing::Not(HasSubstr(std::string(201, 'a'))));
  size_t lines = 0;
  for (char c : text) {
    lines += c == '\n';
  }
  // Two headings, a blank line, 20 chats, and 15 sites.
  EXPECT_EQ(lines, 2u + 20u + 15u);
}

TEST(SuggestionsTest, NothingIsAskedWithoutAModelOrAnythingToGoOn) {
  EXPECT_FALSE(BuildSuggestionsRequestBody("", {"Chat"}, {}));
  EXPECT_FALSE(BuildSuggestionsRequestBody("m", {}, {}));
}

TEST(SuggestionsTest, ReadsTheArray) {
  EXPECT_THAT(
      ParseSuggestionsResponse(Answer(
          R"(["Did my last deploy pass?", "Check my inbox", "Renew it"])")),
      ::testing::Optional(ElementsAre("Did my last deploy pass?",
                                      "Check my inbox", "Renew it")));
}

TEST(SuggestionsTest, ReadsTheArrayInACodeFence) {
  EXPECT_THAT(ParseSuggestionsResponse(
                  Answer("```json\n[\"One\", \"Two\", \"Three\"]\n```")),
              ::testing::Optional(ElementsAre("One", "Two", "Three")));
}

TEST(SuggestionsTest, DropsBlankOverlongAndExtraSuggestions) {
  EXPECT_THAT(ParseSuggestionsResponse(
                  Answer("[\"  One  \", \"\", 7, \"" + std::string(81, 'x') +
                         "\", \"Two\", \"Three\", \"Four\"]")),
              ::testing::Optional(ElementsAre("One", "Two", "Three")));
}

TEST(SuggestionsTest, RefusesAnAnswerThatIsNotAnArray) {
  EXPECT_FALSE(ParseSuggestionsResponse(Answer("Here are some ideas.")));
  EXPECT_FALSE(ParseSuggestionsResponse(Answer(R"({"a": "b"})")));
  EXPECT_FALSE(ParseSuggestionsResponse(Answer("[]")));
  EXPECT_FALSE(ParseSuggestionsResponse("not json"));
}

}  // namespace
}  // namespace uw
