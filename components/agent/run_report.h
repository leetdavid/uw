// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_RUN_REPORT_H_
#define UW_COMPONENTS_AGENT_RUN_REPORT_H_

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace uw {

// The tool a run of a scheduled chat ends with. See docs/product.md,
// "Scheduled chats".
inline constexpr char kReportTool[] = "report";

// How many earlier runs a run is told about.
inline constexpr size_t kPastRuns = 3;

// A value a run records each time, such as a price, so uw can show how it
// changed.
struct TrackedValue {
  std::string name;
  // Such as "USD". May be empty.
  std::string unit;
  // A number, yes or no, or text.
  std::variant<double, bool, std::string> value;

  bool operator==(const TrackedValue&) const = default;
};

// What a run reports: whether it found what the user asked to hear about, a
// short message for the user, and its tracked values.
struct RunReport {
  bool found = false;
  std::string message;
  std::vector<TrackedValue> values;

  bool operator==(const RunReport&) const = default;
};

// An earlier run, as a later one is told about it.
struct PastRun {
  base::Time time;
  RunReport report;
};

// The report tool, as a model is told about it.
base::DictValue ReportToolSchema();

// The report in the arguments of a report call, or what is wrong with them,
// to tell the model.
base::expected<RunReport, std::string> ParseReport(const base::DictValue& args);

// A value as a person reads it, such as "price 549 USD".
std::string DescribeValue(const TrackedValue& value);

// The message a run starts with: the chat's request, the time, and what the
// latest kPastRuns of `past`, newest first, reported.
std::string RunBrief(std::string_view request,
                     base::Time now,
                     const std::vector<PastRun>& past);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_RUN_REPORT_H_
