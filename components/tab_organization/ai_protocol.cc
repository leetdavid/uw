// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace uw {

const char kJevDefaultEndpoint[] = "https://api.typesafe.ai/v1/systemone";
const char kJevModel[] = "jev-latest";
const char kJevNewTaskOption[] = "new_task";
const char kJevScratchpadOption[] = "scratchpad";
const char16_t kAutomaticTaskProvisionalTitle[] = u"New task";
const size_t kMaxGeneratedTaskNameLength = 80;

namespace {

bool ValidProbability(double value) {
  return value >= 0.0 && value <= 1.0;
}

}  // namespace

std::optional<std::string> BuildJevRequestBody(
    const std::string& title,
    const std::string& url,
    const std::string& source_description,
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  if (title.empty() || task_options.empty()) {
    return std::nullopt;
  }
  base::DictValue state;
  state.Set("title", title);
  state.Set("url", url);
  state.Set("source", source_description);
  base::DictValue criteria;
  for (const auto& [key, description] : task_options) {
    if (key.empty()) {
      return std::nullopt;
    }
    criteria.Set(key, description);
  }
  base::DictValue question;
  question.Set("type", "choice");
  question.Set("instructions",
               "Which browsing task does this tab belong to? Choose the "
               "existing task it continues, new_task to create a new task "
               "group, or scratchpad to leave it ungrouped.");
  question.Set("criteria", std::move(criteria));
  base::DictValue questions;
  questions.Set("destination", std::move(question));
  base::DictValue body;
  body.Set("state", std::move(state));
  body.Set("model", kJevModel);
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<JevChoiceAnswer> ParseJevChoiceResponse(
    const std::string& body,
    const std::string& question_id) {
  if (question_id.empty()) {
    return std::nullopt;
  }
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const base::DictValue* answers = parsed->FindDict("answers");
  if (!answers) {
    return std::nullopt;
  }
  const base::DictValue* answer = answers->FindDict(question_id);
  if (!answer) {
    return std::nullopt;
  }
  const std::string* type = answer->FindString("type");
  const std::string* choice = answer->FindString("choice");
  const base::DictValue* probabilities = answer->FindDict("probabilities");
  std::optional<double> confidence = answer->FindDouble("confidence");
  if (!type || *type != "choice" || !choice || choice->empty() ||
      !probabilities || !confidence || !ValidProbability(*confidence)) {
    return std::nullopt;
  }
  JevChoiceAnswer result;
  result.choice = *choice;
  result.confidence = *confidence;
  double total = 0.0;
  for (const auto kv : *probabilities) {
    const base::Value& value = kv.second;
    if (!value.is_double() && !value.is_int()) {
      return std::nullopt;
    }
    const double probability = value.GetDouble();
    if (!ValidProbability(probability)) {
      return std::nullopt;
    }
    result.probabilities.emplace(kv.first, probability);
    total += probability;
  }
  if (result.probabilities.empty() || total < 0.99 || total > 1.01 ||
      probabilities->Find(*choice) == nullptr) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::string> BuildNamingRequestBody(
    const std::string& model,
    const std::string& prompt) {
  if (model.empty() || prompt.empty()) {
    return std::nullopt;
  }
  base::DictValue message;
  message.Set("role", "user");
  message.Set("content", prompt);
  base::ListValue messages;
  messages.Append(std::move(message));
  base::DictValue body;
  body.Set("model", model);
  body.Set("messages", std::move(messages));
  body.Set("max_tokens", 32);
  return base::WriteJson(body);
}

std::optional<std::u16string> ParseNamingResponse(const std::string& body) {
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const base::ListValue* choices = parsed->FindList("choices");
  if (!choices || choices->empty()) {
    return std::nullopt;
  }
  const base::DictValue* message =
      (*choices)[0].GetIfDict()
          ? (*choices)[0].GetIfDict()->FindDict("message")
          : nullptr;
  const std::string* content =
      message ? message->FindString("content") : nullptr;
  if (!content) {
    return std::nullopt;
  }
  std::string trimmed;
  base::TrimWhitespaceASCII(*content, base::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  if (trimmed.size() > kMaxGeneratedTaskNameLength) {
    trimmed.resize(kMaxGeneratedTaskNameLength);
    base::TrimWhitespaceASCII(trimmed, base::TRIM_TRAILING, &trimmed);
  }
  return base::UTF8ToUTF16(trimmed);
}

}  // namespace uw
