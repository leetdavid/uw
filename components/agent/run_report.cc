// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/run_report.h"

#include <algorithm>
#include <utility>

#include "base/i18n/time_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace uw {

namespace {

// Longer messages are cut, so a run's line and notification stay short.
constexpr size_t kMaxMessageLength = 300;
constexpr size_t kMaxValues = 10;

base::DictValue Parameter(std::string_view type, std::string_view description) {
  return base::DictValue().Set("type", type).Set("description", description);
}

std::string When(base::Time time) {
  return base::UnlocalizedTimeFormatWithPattern(time, "EEEE d MMMM y, HH:mm");
}

base::expected<TrackedValue, std::string> ParseValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  const std::string* name = dict ? dict->FindString("name") : nullptr;
  if (!name || base::TrimWhitespaceASCII(*name, base::TRIM_ALL).empty()) {
    return base::unexpected("Each value needs a name.");
  }
  TrackedValue tracked;
  tracked.name = std::string(base::TrimWhitespaceASCII(*name, base::TRIM_ALL));
  if (const std::string* unit = dict->FindString("unit")) {
    tracked.unit =
        std::string(base::TrimWhitespaceASCII(*unit, base::TRIM_ALL));
  }
  const std::optional<double> number = dict->FindDouble("number");
  const std::optional<bool> yes = dict->FindBool("yes");
  const std::string* text = dict->FindString("text");
  if ((number ? 1 : 0) + (yes ? 1 : 0) + (text ? 1 : 0) != 1) {
    return base::unexpected(
        base::StrCat({"The value ", tracked.name,
                      " needs exactly one of number, yes, or text."}));
  }
  if (number) {
    tracked.value = *number;
  } else if (yes) {
    tracked.value = *yes;
  } else {
    tracked.value = *text;
  }
  return tracked;
}

}  // namespace

base::DictValue ReportToolSchema() {
  base::DictValue value_properties =
      base::DictValue()
          .Set("name", Parameter("string",
                                 "A short name, such as price or in_stock. Use "
                                 "the names earlier runs used."))
          .Set("unit", Parameter("string", "Such as USD. Leave out when none."))
          .Set("number", Parameter("number", "The value, when it is a number."))
          .Set("yes", Parameter("boolean", "The value, when it is yes or no."))
          .Set("text", Parameter("string", "The value, when it is text."));
  base::DictValue properties =
      base::DictValue()
          .Set("found",
               Parameter("boolean",
                         "True only when this run found what the user asked "
                         "to hear about, such as a lower price or a new "
                         "story."))
          .Set("message",
               Parameter("string",
                         "One or two short sentences for the user about what "
                         "this run found."))
          .Set(
              "values",
              base::DictValue()
                  .Set("type", "array")
                  .Set("description",
                       "The numbers or facts this request tracks, so the "
                       "user can see them change over time.")
                  .Set("items",
                       base::DictValue()
                           .Set("type", "object")
                           .Set("properties", std::move(value_properties))
                           .Set("required", base::ListValue().Append("name"))));
  return base::DictValue()
      .Set("type", "function")
      .Set("function",
           base::DictValue()
               .Set("name", kReportTool)
               .Set("description",
                    "Reports what this scheduled run found. Call it once, "
                    "when you are done, before your last reply.")
               .Set("parameters",
                    base::DictValue()
                        .Set("type", "object")
                        .Set("properties", std::move(properties))
                        .Set("required",
                             base::ListValue().Append("found").Append(
                                 "message"))));
}

base::expected<RunReport, std::string> ParseReport(
    const base::DictValue& args) {
  RunReport report;
  const std::optional<bool> found = args.FindBool("found");
  if (!found) {
    return base::unexpected("report needs found, true or false.");
  }
  report.found = *found;
  const std::string* message = args.FindString("message");
  if (!message || base::TrimWhitespaceASCII(*message, base::TRIM_ALL).empty()) {
    return base::unexpected("report needs a message for the user.");
  }
  report.message =
      std::string(base::TrimWhitespaceASCII(*message, base::TRIM_ALL));
  if (report.message.size() > kMaxMessageLength) {
    base::TruncateUTF8ToByteSize(report.message, kMaxMessageLength,
                                 &report.message);
  }
  if (const base::ListValue* values = args.FindList("values")) {
    for (const base::Value& value : *values) {
      if (report.values.size() == kMaxValues) {
        break;
      }
      base::expected<TrackedValue, std::string> tracked = ParseValue(value);
      if (!tracked.has_value()) {
        return base::unexpected(std::move(tracked.error()));
      }
      report.values.push_back(std::move(*tracked));
    }
  }
  return report;
}

std::string DescribeValue(const TrackedValue& value) {
  std::string text;
  if (const double* number = std::get_if<double>(&value.value)) {
    text = base::NumberToString(*number);
  } else if (const bool* yes = std::get_if<bool>(&value.value)) {
    text = *yes ? "yes" : "no";
  } else {
    text = std::get<std::string>(value.value);
  }
  return value.unit.empty()
             ? base::StrCat({value.name, " ", text})
             : base::StrCat({value.name, " ", text, " ", value.unit});
}

std::string RunBrief(std::string_view request,
                     base::Time now,
                     const std::vector<PastRun>& past) {
  std::string brief =
      base::StrCat({"This is a scheduled run of the user's request. It is ",
                    When(now), ".\n\nThe request: ", request, "\n\n"});
  if (past.empty()) {
    brief += "This is the first run.\n\n";
  } else {
    brief += "What earlier runs reported, newest first:\n";
    std::vector<const PastRun*> latest;
    for (const PastRun& run : past) {
      latest.push_back(&run);
    }
    std::ranges::sort(latest, std::ranges::greater(), &PastRun::time);
    latest.resize(std::min(latest.size(), kPastRuns));
    for (const PastRun* run : latest) {
      brief += base::StrCat({"- ", When(run->time), ": ",
                             run->report.found ? "found it. " : "",
                             run->report.message});
      for (size_t i = 0; i < run->report.values.size(); ++i) {
        brief += base::StrCat(
            {i == 0 ? " (" : ", ", DescribeValue(run->report.values[i])});
      }
      brief += run->report.values.empty() ? "\n" : ")\n";
    }
    brief += "\n";
  }
  brief +=
      "Do what the request asks, with your tools, and compare with the "
      "earlier runs. Then call report once, and reply in one short sentence.";
  return brief;
}

}  // namespace uw
