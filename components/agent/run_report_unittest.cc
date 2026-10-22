// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/run_report.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/test/icu_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

base::DictValue Args(std::string_view json) {
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC).value();
}

base::Time Utc(int year, int month, int day, int hour, int minute = 0) {
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  base::Time time;
  EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
  return time;
}

TEST(RunReportTest, ReadsWhatTheRunFoundAndItsValues) {
  const base::expected<RunReport, std::string> report = ParseReport(Args(R"({
    "found": true,
    "message": "  The desk is $479, below your $500.  ",
    "values": [
      {"name": "price", "unit": "USD", "number": 479},
      {"name": "in_stock", "yes": true},
      {"name": "sale", "text": "ends Sunday"}
    ]
  })"));
  ASSERT_TRUE(report.has_value()) << report.error();
  EXPECT_TRUE(report->found);
  EXPECT_EQ(report->message, "The desk is $479, below your $500.");
  ASSERT_EQ(report->values.size(), 3u);
  EXPECT_EQ(report->values[0], (TrackedValue{"price", "USD", 479.0}));
  EXPECT_EQ(report->values[1], (TrackedValue{"in_stock", "", true}));
  EXPECT_EQ(report->values[2],
            (TrackedValue{"sale", "", std::string("ends Sunday")}));
}

TEST(RunReportTest, ValuesAreOptional) {
  const base::expected<RunReport, std::string> report =
      ParseReport(Args(R"({"found": false, "message": "No new stories."})"));
  ASSERT_TRUE(report.has_value());
  EXPECT_FALSE(report->found);
  EXPECT_TRUE(report->values.empty());
}

TEST(RunReportTest, ReadsASiteThatWantsASignIn) {
  const base::expected<RunReport, std::string> report = ParseReport(
      Args(R"({"found": false, "message": "fully.com asks me to sign in.",
               "sign_in": " fully.com "})"));
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->sign_in, "fully.com");
}

TEST(RunReportTest, TellsTheModelWhatIsMissingOrWrong) {
  EXPECT_EQ(ParseReport(Args(R"({"message": "Hi"})")).error(),
            "report needs found, true or false.");
  EXPECT_EQ(ParseReport(Args(R"({"found": true, "message": "  "})")).error(),
            "report needs a message for the user.");
  EXPECT_EQ(ParseReport(Args(R"({"found": true, "message": "Hi",
                                 "values": [{"number": 3}]})"))
                .error(),
            "Each value needs a name.");
  EXPECT_EQ(ParseReport(Args(R"({"found": true, "message": "Hi",
                                 "values": [{"name": "price", "number": 3,
                                             "text": "three"}]})"))
                .error(),
            "The value price needs exactly one of number, yes, or text.");
}

TEST(RunReportTest, DescribesValuesAsAPersonReadsThem) {
  EXPECT_EQ(DescribeValue({"price", "USD", 549.0}), "price 549 USD");
  EXPECT_EQ(DescribeValue({"price", "USD", 549.99}), "price 549.99 USD");
  EXPECT_EQ(DescribeValue({"in_stock", "", false}), "in_stock no");
}

TEST(RunReportTest, TheBriefHasTheRequestTheTimeAndTheLatestRuns) {
  base::test::ScopedRestoreDefaultTimezone zone("America/Los_Angeles");
  std::vector<PastRun> past;
  for (int day = 18; day <= 21; ++day) {
    past.push_back({Utc(2026, 10, day, 16),
                    {false, "Still above $500.", {{"price", "USD", 549.0}}}});
  }
  past.push_back({Utc(2026, 10, 22, 16),
                  {true, "Down to $479.", {{"price", "USD", 479.0}}}});

  const std::string brief =
      RunBrief("Tell me when the Jarvis desk drops below $500.", "",
               Utc(2026, 10, 23, 16), past);

  EXPECT_THAT(brief, HasSubstr("It is Friday 23 October 2026, 09:00."));
  EXPECT_THAT(brief, HasSubstr("The request: Tell me when the Jarvis desk "
                               "drops below $500."));
  EXPECT_THAT(brief, HasSubstr("- Thursday 22 October 2026, 09:00: found it. "
                               "Down to $479. (price 479 USD)\n"
                               "- Wednesday 21 October 2026, 09:00: Still "
                               "above $500. (price 549 USD)\n"
                               "- Tuesday 20 October 2026, 09:00:"));
  EXPECT_THAT(brief, Not(HasSubstr("Monday 19 October")));
  EXPECT_THAT(brief, HasSubstr("call report once"));
  EXPECT_THAT(brief, Not(HasSubstr("The page it watches")));
}

TEST(RunReportTest, TheBriefNamesAWatchedPage) {
  EXPECT_THAT(
      RunBrief("Tell me when the price drops.",
               "https://fully.com/desks/jarvis", Utc(2026, 10, 23, 16), {}),
      HasSubstr("The page it watches: https://fully.com/desks/jarvis"));
}

TEST(RunReportTest, TheFirstRunIsToldItIsTheFirst) {
  EXPECT_THAT(RunBrief("Summarize Hacker News.", "", Utc(2026, 10, 23, 14), {}),
              HasSubstr("This is the first run."));
}

}  // namespace
}  // namespace uw
