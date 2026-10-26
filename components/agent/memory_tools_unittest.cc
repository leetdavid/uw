// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/memory_tools.h"

#include <string>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Not;

std::vector<std::string> Names(const base::ListValue& tools) {
  std::vector<std::string> names;
  for (const base::Value& tool : tools) {
    names.push_back(*tool.GetDict().FindStringByDottedPath("function.name"));
  }
  return names;
}

TEST(MemoryToolsTest, AChatThatKeepsGetsAllThreeToolsAndARunOnlySearches) {
  EXPECT_THAT(Names(MemoryToolSchemas(true)),
              ElementsAre("search_memory", "remember", "forget"));
  EXPECT_THAT(Names(MemoryToolSchemas(false)), ElementsAre("search_memory"));
}

TEST(MemoryToolsTest, TheInstructionsListTheCoreMemoriesWithTheirIds) {
  Memory city;
  city.id = 4;
  city.text = "Lives in the Inner Sunset, San Francisco.";
  Memory name;
  name.id = 9;
  name.text = "Goes by Sam.";

  const std::string keeps = MemoryInstructions({city, name}, true);
  EXPECT_THAT(keeps, HasSubstr("call remember"));
  EXPECT_THAT(keeps,
              EndsWith("Core memories:\n"
                       "- [4] Lives in the Inner Sunset, San Francisco.\n"
                       "- [9] Goes by Sam."));

  const std::string recalls = MemoryInstructions({}, false);
  EXPECT_THAT(recalls, Not(HasSubstr("call remember")));
  EXPECT_THAT(recalls, HasSubstr("cannot keep or forget"));
  EXPECT_THAT(recalls, EndsWith("Core memories: none yet."));
}

TEST(MemoryToolsTest, SearchWordsAreLowercaseOnceEachAndAtMostTen) {
  EXPECT_THAT(MemorySearchWords("Bank, card; BANK payment a"),
              ElementsAre("bank", "card", "payment"));
  EXPECT_EQ(MemorySearchWords("a b c d e f g h i j k l m n o p q r s t u v w "
                              "aa bb cc dd ee ff gg hh ii jj kk ll")
                .size(),
            10u);
  EXPECT_TRUE(MemorySearchWords(" , . ").empty());
}

TEST(MemoryToolsTest, AMemoryIsGivenWithItsId) {
  Memory memory;
  memory.id = 12;
  memory.text = "Banks with Chase.";
  const base::DictValue value = MemoryToValue(memory);
  EXPECT_EQ(value.FindInt("id"), 12);
  EXPECT_EQ(*value.FindString("memory"), "Banks with Chase.");
}

}  // namespace
}  // namespace uw
