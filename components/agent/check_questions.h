// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_CHECK_QUESTIONS_H_
#define UW_COMPONENTS_AGENT_CHECK_QUESTIONS_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/values.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

// What the command check and the memory check share: each asks a model a few
// questions with fixed options about one step of the agent, and reads how
// likely the model finds each option. A decision model takes the questions in
// the Decisions or System One format; a language model answers a JSON object
// whose shape each check describes. No networking here.

// Each option's value and its description.
using CheckOptions = std::vector<std::pair<std::string, std::string>>;

struct CheckQuestion {
  std::string name;
  std::string instructions;
  CheckOptions options;
};

// The request asking a decision model `questions` about `input`, in
// kDecisions or kSystemOne.
std::optional<std::string> BuildCheckQuestionsBody(
    DecisionFormat format,
    std::string_view model,
    std::string_view input,
    const std::vector<CheckQuestion>& questions);

// The answer to the question `name` in a decision model's `response`, or null.
const base::DictValue* FindCheckAnswer(DecisionFormat format,
                                       const base::DictValue& response,
                                       std::string_view name);

// The JSON object in a language model's chat-completions `body`, alone or
// inside a Markdown code fence.
std::optional<base::DictValue> ReadCheckChatAnswer(std::string_view body);

bool IsCheckOption(const CheckOptions& options, std::string_view value);
bool IsProbability(double value);

// Whether `answer` chose one of `options`.
bool ChoseCheckOption(const base::DictValue* answer,
                      const CheckOptions& options);

// The probability the table of `answer` lists for `option`. The Decisions API
// lists them as {value, probability} pairs, System One as a dictionary.
std::optional<double> ListedProbability(const base::DictValue& answer,
                                        std::string_view option);

// The probability `answer` gives `option`: from its table when it lists one,
// or else from its choice and confidence.
std::optional<double> ProbabilityOfOption(const base::DictValue& answer,
                                          std::string_view option);

// `text` on one line, trimmed and cut to `max_length` bytes.
std::string OneLine(std::string_view text, size_t max_length);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_CHECK_QUESTIONS_H_
