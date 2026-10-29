// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/tab_tools.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

base::DictValue Args(std::string_view json) {
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC).value();
}

base::Time Now() {
  return base::Time::FromSecondsSinceUnixEpoch(1793000000);
}

ListedTab Tab(std::string id,
              std::string title,
              std::string url,
              std::string task_id = "",
              std::string task = "") {
  ListedTab tab;
  tab.id = std::move(id);
  tab.title = std::move(title);
  tab.url = std::move(url);
  tab.workspace = "Personal";
  tab.task_id = std::move(task_id);
  tab.task = std::move(task);
  return tab;
}

// A pin, a task of two, one nested, and the Scratchpad with the chat and a
// busy terminal.
std::vector<ListedTab> Tabs() {
  ListedTab pin = Tab("f0e3a6c1-77aa-4bcd-9e10-1a2b3c4d5e6f", "Gmail",
                      "https://mail.google.com/mail/u/0/");
  pin.pinned = true;
  pin.workspace.clear();
  ListedTab booking =
      Tab("3f9a1c07-1111-4bcd-9e10-1a2b3c4d5e6f", "TAP Air Portugal",
          "https://www.flytap.com/en-us/manage",
          "9C2E41AB00000000000000000000000A", "Flights");
  booking.selected = true;
  ListedTab kayak = Tab("18a7e0b2-2222-4bcd-9e10-1a2b3c4d5e6f",
                        "SFO to LIS · KAYAK", "https://www.kayak.com/flights",
                        "9C2E41AB00000000000000000000000A", "Flights");
  kayak.depth = 1;
  kayak.seen = Now() - base::Days(6);
  ListedTab chat = Tab("0c4e8d71-3333-4bcd-9e10-1a2b3c4d5e6f",
                       "Tidy my trip tabs", "chrome://chat/0c4e");
  chat.chat = true;
  ListedTab terminal = Tab("e2b9f604-4444-4bcd-9e10-1a2b3c4d5e6f",
                           "~/dev/shop-web", "chrome://terminal/");
  terminal.busy = true;
  terminal.agent_tab = 2;
  return {pin, booking, kayak, chat, terminal};
}

TEST(TabToolsTest, TheListHasHeadingsAndOneLinePerTab) {
  const base::DictValue result = TabListResult(Tabs(), Now());
  EXPECT_EQ(result.FindInt("tabs"), 5);
  EXPECT_EQ(result.FindInt("workspaces"), 1);
  EXPECT_EQ(*result.FindString("list"),
            "Pinned\n"
            "  f0e3a6c1  Gmail · mail.google.com\n"
            "Personal › Flights (task 9c2e41ab)\n"
            "  3f9a1c07  TAP Air Portugal · flytap.com · selected\n"
            "    18a7e0b2  SFO to LIS · KAYAK · kayak.com · 6 days ago\n"
            "Personal › Scratchpad\n"
            "  0c4e8d71  Tidy my trip tabs · uw://chat · this chat\n"
            "  e2b9f604  ~/dev/shop-web · terminal · your tab 2 · busy");
}

TEST(TabToolsTest, AnotherWindowsTreeIsNamedByItsWindow) {
  ListedTab tab = Tabs()[3];
  tab.tree = 2;
  EXPECT_EQ(PlaceOf(tab), "Personal (window 2) › Scratchpad");
}

TEST(TabToolsTest, TheListStopsAtItsLimit) {
  std::vector<ListedTab> tabs(kMaxListedTabs + 40,
                              Tab("aaaaaaaa", "Page", "https://a.test/"));
  const base::DictValue result = TabListResult(tabs, Now());
  EXPECT_EQ(result.FindInt("tabs"), static_cast<int>(kMaxListedTabs) + 40);
  EXPECT_THAT(*result.FindString("list"),
              HasSubstr("… and 40 more. Narrow the list with query"));
}

TEST(TabToolsTest, SeenSaysHowLongAgo) {
  EXPECT_EQ(SeenAgo(Now() - base::Seconds(20), Now()), "now");
  EXPECT_EQ(SeenAgo(Now() - base::Minutes(1), Now()), "1 minute ago");
  EXPECT_EQ(SeenAgo(Now() - base::Hours(5), Now()), "5 hours ago");
  EXPECT_EQ(SeenAgo(Now() - base::Days(3), Now()), "3 days ago");
}

TEST(TabToolsTest, FiltersKeepEveryWordAndTheWorkspaceAndTask) {
  const std::vector<ListedTab> tabs = Tabs();
  EXPECT_TRUE(
      Matches(tabs[2], TabFilterFrom(Args(R"({"query": "kayak sfo"})"))));
  EXPECT_FALSE(
      Matches(tabs[2], TabFilterFrom(Args(R"({"query": "kayak hotel"})"))));
  EXPECT_TRUE(Matches(tabs[1], TabFilterFrom(Args(R"({"task": "flights"})"))));
  EXPECT_TRUE(Matches(tabs[1], TabFilterFrom(Args(R"({"task": "9c2e"})"))));
  EXPECT_FALSE(Matches(tabs[3], TabFilterFrom(Args(R"({"task": "9c2e"})"))));
  EXPECT_FALSE(
      Matches(tabs[1], TabFilterFrom(Args(R"({"workspace": "Work"})"))));
}

TEST(TabToolsTest, AnyUniqueStartOfAnIdNamesATab) {
  const std::vector<ListedTab> tabs = Tabs();
  EXPECT_EQ(FindTabs(tabs, Args(R"({"tabs": ["3F9A", "e2b9f604", 2]})")),
            (std::vector<size_t>{1, 4}));
  EXPECT_EQ(FindTab(tabs, Args(R"({"tab": "18a7e0b2"})")), 2u);
}

TEST(TabToolsTest, AnIdThatMatchesNoneOrSeveralIsRefused) {
  std::vector<ListedTab> tabs = Tabs();
  tabs.push_back(Tab("3f9a77e2-5555-4bcd-9e10-1a2b3c4d5e6f", "Other",
                     "https://other.test/"));
  const auto several = FindTabs(tabs, Args(R"({"tabs": ["3f9a"]})"));
  ASSERT_FALSE(several.has_value());
  EXPECT_EQ(several.error().reason, "it matches 2 tabs");
  EXPECT_EQ(*RefusalResult(kCloseTool, several.error()).FindString("error"),
            "Didn't close 3f9a: it matches 2 tabs.");
  const auto none = FindTabs(tabs, Args(R"({"tabs": ["zzzz"]})"));
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().reason, "no tab has that id");
  EXPECT_FALSE(FindTabs(tabs, Args(R"({"tabs": []})")).has_value());
  EXPECT_FALSE(FindTabs(tabs, Args(R"({"tabs": [7]})")).has_value());
}

TEST(TabToolsTest, ATaskIsNamedByItsIdOrItsWholeName) {
  const std::vector<ListedTab> tabs = Tabs();
  EXPECT_EQ(FindTask(tabs, Args(R"({"task": "9c2e41ab"})"), "task"), 1u);
  EXPECT_EQ(FindTask(tabs, Args(R"({"task": "flights"})"), "task"), 1u);
  EXPECT_FALSE(
      FindTask(tabs, Args(R"({"task": "Flight"})"), "task").has_value());
}

TEST(TabToolsTest, ARunHasNoSelect) {
  const auto names = [](const base::ListValue& tools) {
    std::string all;
    for (const base::Value& tool : tools) {
      all += *tool.GetDict().FindStringByDottedPath("function.name") + " ";
    }
    return all;
  };
  EXPECT_EQ(names(TabToolSchemas(/*select=*/true)),
            "tabs select move rename_task close close_task ");
  EXPECT_THAT(names(TabToolSchemas(/*select=*/false)),
              Not(HasSubstr("select")));
}

}  // namespace
}  // namespace uw
