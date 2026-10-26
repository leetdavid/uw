// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/check_questions.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "uw/components/agent/agent_protocol.h"

namespace uw {

std::optional<std::string> BuildCheckQuestionsBody(
    DecisionFormat format,
    std::string_view model,
    std::string_view input,
    const std::vector<CheckQuestion>& questions) {
  if (format == DecisionFormat::kSystemOne) {
    base::DictValue asked;
    for (const CheckQuestion& question : questions) {
      base::DictValue criteria;
      for (const auto& [key, description] : question.options) {
        criteria.Set(key, description);
      }
      asked.Set(question.name, base::DictValue()
                                   .Set("type", "choice")
                                   .Set("instructions", question.instructions)
                                   .Set("criteria", std::move(criteria)));
    }
    return base::WriteJson(base::DictValue()
                               .Set("model", model)
                               .Set("state", input)
                               .Set("questions", std::move(asked)));
  }
  base::ListValue asked;
  for (const CheckQuestion& question : questions) {
    base::ListValue choices;
    for (const auto& [value, description] : question.options) {
      choices.Append(base::DictValue()
                         .Set("value", value)
                         .Set("description", description));
    }
    asked.Append(base::DictValue()
                     .Set("type", "choice")
                     .Set("name", question.name)
                     .Set("instructions", question.instructions)
                     .Set("choices", std::move(choices)));
  }
  return base::WriteJson(base::DictValue()
                             .Set("model", model)
                             .Set("input", input)
                             .Set("questions", std::move(asked)));
}

const base::DictValue* FindCheckAnswer(DecisionFormat format,
                                       const base::DictValue& response,
                                       std::string_view name) {
  if (format == DecisionFormat::kSystemOne) {
    const base::DictValue* answers = response.FindDict("answers");
    return answers ? answers->FindDict(name) : nullptr;
  }
  const base::ListValue* answers = response.FindList("answers");
  if (!answers) {
    return nullptr;
  }
  for (const base::Value& item : *answers) {
    const base::DictValue* answer = item.GetIfDict();
    const std::string* asked = answer ? answer->FindString("name") : nullptr;
    if (asked && *asked == name) {
      return answer;
    }
  }
  return nullptr;
}

std::optional<base::DictValue> ReadCheckChatAnswer(std::string_view body) {
  const std::optional<ModelTurn> turn = ParseAgentResponse(body);
  if (!turn) {
    return std::nullopt;
  }
  std::string_view text = base::TrimWhitespaceASCII(turn->text, base::TRIM_ALL);
  if (text.starts_with("```")) {
    const size_t newline = text.find('\n');
    if (newline == std::string_view::npos) {
      return std::nullopt;
    }
    text = text.substr(newline + 1);
    if (const size_t fence = text.rfind("```"); fence != std::string::npos) {
      text = text.substr(0, fence);
    }
  }
  return base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
}

bool IsCheckOption(const CheckOptions& options, std::string_view value) {
  return std::ranges::any_of(
      options, [value](const auto& option) { return option.first == value; });
}

bool IsProbability(double value) {
  return value >= 0.0 && value <= 1.0;
}

bool ChoseCheckOption(const base::DictValue* answer,
                      const CheckOptions& options) {
  const std::string* choice = answer ? answer->FindString("choice") : nullptr;
  return choice && IsCheckOption(options, *choice);
}

std::optional<double> ListedProbability(const base::DictValue& answer,
                                        std::string_view option) {
  if (const base::DictValue* table = answer.FindDict("probabilities")) {
    return table->FindDouble(option);
  }
  if (const base::ListValue* pairs = answer.FindList("probabilities")) {
    for (const base::Value& item : *pairs) {
      const base::DictValue* pair = item.GetIfDict();
      const std::string* value = pair ? pair->FindString("value") : nullptr;
      if (value && *value == option) {
        return pair->FindDouble("probability");
      }
    }
  }
  return std::nullopt;
}

std::optional<double> ProbabilityOfOption(const base::DictValue& answer,
                                          std::string_view option) {
  const std::optional<double> listed = ListedProbability(answer, option);
  if (listed) {
    return IsProbability(*listed) ? listed : std::nullopt;
  }
  const std::string* choice = answer.FindString("choice");
  const std::optional<double> confidence = answer.FindDouble("confidence");
  if (!choice || !confidence || !IsProbability(*confidence)) {
    return std::nullopt;
  }
  return *choice == option ? *confidence : 1.0 - *confidence;
}

std::string OneLine(std::string_view text, size_t max_length) {
  std::string line;
  base::ReplaceChars(base::TrimWhitespaceASCII(text, base::TRIM_ALL), "\r\n",
                     " ", &line);
  std::string cut;
  base::TruncateUTF8ToByteSize(line, max_length, &cut);
  return cut;
}

}  // namespace uw
