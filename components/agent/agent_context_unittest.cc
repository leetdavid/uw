// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/agent_context.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

base::DictValue Tab(int tab) {
  return base::DictValue().Set("tab", tab);
}

base::DictValue Reading(std::string_view text) {
  return base::DictValue()
      .Set("title", "Book a tour")
      .Set("url", "https://shop.test/form")
      .Set("text", text);
}

int Count(const std::string& body, std::string_view text) {
  int count = 0;
  for (size_t at = body.find(text); at != std::string::npos;
       at = body.find(text, at + 1)) {
    ++count;
  }
  return count;
}

class AgentContextTest : public testing::Test {
 protected:
  std::string Request(size_t max_length = 200000) {
    std::optional<std::string> body = context_.Request("agent", max_length);
    EXPECT_TRUE(body);
    return body.value_or("");
  }

  // What each tool message tells the model, in order.
  std::vector<base::DictValue> ToolContents() {
    std::vector<base::DictValue> contents;
    std::optional<base::DictValue> request =
        base::JSONReader::ReadDict(Request(), base::JSON_PARSE_RFC);
    for (const base::Value& message : *request->FindList("messages")) {
      if (*message.GetDict().FindString("role") == "tool") {
        contents.push_back(*base::JSONReader::ReadDict(
            *message.GetDict().FindString("content"), base::JSON_PARSE_RFC));
      }
    }
    return contents;
  }

  AgentContext context_;
};

TEST_F(AgentContextTest, ARereadTabSendsOnlyItsLatestReading) {
  context_.AddUser("Book a tour");
  context_.AddResult("call_1", "read", Tab(1), Reading("Tours start at noon"));
  context_.AddResult("call_2", "read", Tab(2), Reading("Tickets are sold out"));
  context_.AddResult("call_3", "read", Tab(1), Reading("Tours start at one"));

  const std::string body = Request();

  EXPECT_THAT(body, Not(HasSubstr("Tours start at noon")));
  EXPECT_THAT(body, HasSubstr("Tours start at one"));
  EXPECT_THAT(body, HasSubstr("Tickets are sold out"));
  EXPECT_EQ(*ToolContents()[0].FindString("shortened"),
            "Step 1 read tab 1, \"Book a tour\" at https://shop.test/form: 19 "
            "characters, shortened. Call recall with step 1 to see it again.");
  EXPECT_EQ(*ToolContents()[2].FindString("text"), "Tours start at one");
  EXPECT_EQ(ToolContents()[2].FindInt("step"), 3);
}

TEST_F(AgentContextTest, AnAnswerShortensEveryLongResult) {
  context_.AddUser("Book a tour");
  context_.AddResult("call_1", "open", base::DictValue(), Tab(1));
  context_.AddResult("call_2", "read", Tab(1), Reading("Tours start at noon"));
  context_.AddResult("call_3", "navigate", Tab(1),
                     base::DictValue().Set("note", std::string(400, 'x')));
  context_.AddAssistant({"Booked.", {}});

  context_.ShortenAll();
  const std::string body = Request();

  EXPECT_THAT(body, Not(HasSubstr("Tours start at noon")));
  EXPECT_THAT(body, Not(HasSubstr(std::string(400, 'x'))));
  const std::vector<base::DictValue> contents = ToolContents();
  EXPECT_EQ(contents[0], base::DictValue().Set("step", 1).Set("tab", 1));
  EXPECT_THAT(*contents[2].FindString("shortened"),
              HasSubstr("Step 3 used navigate: "));
  EXPECT_THAT(body, HasSubstr("Book a tour"));
  EXPECT_THAT(body, HasSubstr("Booked."));
}

TEST_F(AgentContextTest, RecallBringsBackAShortenedStep) {
  context_.AddResult("call_1", "read", Tab(1), Reading("Tours start at noon"));
  context_.ShortenAll();

  const base::DictValue recalled =
      context_.Recall(base::DictValue().Set("step", 1));

  EXPECT_EQ(*recalled.FindStringByDottedPath("result.text"),
            "Tours start at noon");
  EXPECT_EQ(*recalled.FindString("tool"), "read");
  EXPECT_THAT(*recalled.FindString("note"),
              HasSubstr("read the tab again to act"));
  EXPECT_EQ(
      *context_.Recall(base::DictValue().Set("step", 2)).FindString("error"),
      "There is no step 2.");
  EXPECT_EQ(*context_.Recall(base::DictValue()).FindString("error"),
            "There is no step like that.");
}

TEST_F(AgentContextTest, ARequestShortensItsOldestReadingsToFit) {
  context_.AddUser("Compare three tours");
  for (int tab = 1; tab <= 3; ++tab) {
    context_.AddResult(base::StrCat({"call_", base::NumberToString(tab)}),
                       "read", Tab(tab),
                       Reading(base::StrCat({"Tour ", base::NumberToString(tab),
                                             " ", std::string(1000, 'x')})));
  }
  const size_t full = Request().size();

  const std::string body = Request(full - 500);

  EXPECT_LE(body.size(), full - 500);
  EXPECT_EQ(Count(body, std::string(1000, 'x')), 2);
  EXPECT_THAT(body, Not(HasSubstr("Tour 1 ")));
  EXPECT_THAT(body, HasSubstr("Tour 3 "));
}

TEST_F(AgentContextTest, AFailedReadingIsLeftAsItIs) {
  context_.AddResult("call_1", "read", Tab(1), Reading("Tours start at noon"));
  context_.AddResult("call_2", "read", Tab(1),
                     base::DictValue().Set("error", "That tab is closed."));
  context_.ShortenAll();

  EXPECT_THAT(Request(), HasSubstr("That tab is closed."));
}

}  // namespace
}  // namespace uw
