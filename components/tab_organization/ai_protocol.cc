// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace uw {

const char kNewTaskOption[] = "new_task";
const char kScratchpadOption[] = "scratchpad";
const char16_t kAutomaticTaskProvisionalTitle[] = u"New task";
const size_t kMaxGeneratedTaskNameLength = 80;
const size_t kMaxExcerptLength = 2000;

namespace {

constexpr char kSystemPrompt[] =
    "You organize browser tabs into browsing tasks. The user message "
    "describes one tab branch with its title, URL, source-tab relationship, "
    "an optional readable-text excerpt, and the current task list. Decide "
    "where the branch belongs.\n"
    "Respond with only a JSON object in this exact shape:\n"
    "{\"decision\": \"move\" | \"new_task\" | \"leave\", "
    "\"target\": \"<task key>\", \"confidence\": <number 0..1>, "
    "\"name\": \"<short task name>\"}\n"
    "- \"move\": the branch clearly continues one existing task. Set "
    "\"target\" to that task's key.\n"
    "- \"new_task\": the branch is clearly a separate browsing task. Set "
    "\"name\" to a short action-oriented phrase of at most five words.\n"
    "- \"leave\": keep the branch ungrouped in the Scratchpad.\n"
    "Prefer \"leave\" when unsure. Only move or create at high confidence "
    "(0.90 or higher). If the description says the user manually placed the "
    "branch, move it only when its activity clearly changed.";

constexpr char kDestinationQuestion[] = "destination";
constexpr size_t kMaxDecisionChoices = 255;
constexpr char kDecisionInstructions[] =
    "Decide which browsing task this tab branch belongs to. Choose an "
    "existing task only when the branch clearly continues it. Choose new_task "
    "when it clearly starts a separate browsing task. Choose scratchpad when "
    "unsure, or when the user manually placed the branch and its activity has "
    "not clearly changed.";
constexpr char kNewTaskDescription[] =
    "Start a new browsing task for this branch.";
constexpr char kScratchpadDescription[] =
    "Leave the branch ungrouped in the Scratchpad.";
constexpr char kNamingPrompt[] =
    "Name the browsing task that this tab starts. Reply with only the name: a "
    "short, action-oriented phrase of at most five words, with no quotes.";

bool ValidConfidence(double value) {
  return value >= 0.0 && value <= 1.0;
}

bool ValidTaskOptions(
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  for (const auto& [key, name] : task_options) {
    if (key.empty() || key == kNewTaskOption || key == kScratchpadOption) {
      return false;
    }
  }
  return true;
}

std::string DescribeBranch(const BranchContext& context) {
  std::string text = "Title: " + context.title + "\nURL: " + context.url +
                     "\nSource: " + context.source_description;
  if (!context.manual_placement.empty()) {
    text += "\nManual placement: " + context.manual_placement;
  }
  if (!context.excerpt.empty()) {
    text += "\nPage excerpt: " + context.excerpt;
  }
  return text;
}

const std::string* FirstMessageContent(const base::DictValue& response) {
  const base::ListValue* choices = response.FindList("choices");
  if (!choices || choices->empty()) {
    return nullptr;
  }
  const base::DictValue* choice = (*choices)[0].GetIfDict();
  const base::DictValue* message = choice ? choice->FindDict("message") : nullptr;
  return message ? message->FindString("content") : nullptr;
}

std::optional<ClassificationAnswer> ChoiceAnswer(
    const base::DictValue& answer,
    std::optional<double> fallback_confidence) {
  const std::string* type = answer.FindString("type");
  const std::string* choice = answer.FindString("choice");
  if (!type || *type != "choice" || !choice || choice->empty()) {
    return std::nullopt;
  }
  std::optional<double> confidence = answer.FindDouble("confidence");
  if (!confidence) {
    confidence = fallback_confidence;
  }
  if (!confidence || !ValidConfidence(*confidence)) {
    return std::nullopt;
  }
  ClassificationAnswer result;
  result.choice = *choice;
  result.confidence = *confidence;
  return result;
}

std::optional<double> ListedProbability(const base::DictValue& answer) {
  const std::string* choice = answer.FindString("choice");
  const base::ListValue* probabilities = answer.FindList("probabilities");
  if (!choice || !probabilities) {
    return std::nullopt;
  }
  for (const base::Value& item : *probabilities) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* value = entry ? entry->FindString("value") : nullptr;
    if (value && *value == *choice) {
      return entry->FindDouble("probability");
    }
  }
  return std::nullopt;
}

std::optional<std::string> BuildDecisionsRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  base::ListValue choices;
  const auto add = [&choices](const std::string& value,
                              const std::string& description) {
    base::DictValue choice;
    choice.Set("value", value);
    choice.Set("description", description);
    choices.Append(std::move(choice));
  };
  for (const auto& [key, name] : task_options) {
    add(key, name);
  }
  add(kNewTaskOption, kNewTaskDescription);
  add(kScratchpadOption, kScratchpadDescription);
  base::DictValue question;
  question.Set("type", "choice");
  question.Set("name", kDestinationQuestion);
  question.Set("instructions", kDecisionInstructions);
  question.Set("choices", std::move(choices));
  base::ListValue questions;
  questions.Append(std::move(question));
  base::DictValue body;
  body.Set("model", model);
  body.Set("input", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<std::string> BuildSystemOneRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  base::DictValue criteria;
  for (const auto& [key, name] : task_options) {
    criteria.Set(key, name);
  }
  criteria.Set(kNewTaskOption, kNewTaskDescription);
  criteria.Set(kScratchpadOption, kScratchpadDescription);
  base::DictValue question;
  question.Set("type", "choice");
  question.Set("instructions", kDecisionInstructions);
  question.Set("criteria", std::move(criteria));
  base::DictValue questions;
  questions.Set(kDestinationQuestion, std::move(question));
  base::DictValue body;
  body.Set("model", model);
  body.Set("state", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<ClassificationAnswer> ParseDecisionsResponse(
    const base::DictValue& response) {
  const base::ListValue* answers = response.FindList("answers");
  if (!answers) {
    return std::nullopt;
  }
  for (const base::Value& item : *answers) {
    const base::DictValue* answer = item.GetIfDict();
    const std::string* name = answer ? answer->FindString("name") : nullptr;
    if (name && *name == kDestinationQuestion) {
      return ChoiceAnswer(*answer, ListedProbability(*answer));
    }
  }
  return std::nullopt;
}

std::optional<ClassificationAnswer> ParseSystemOneResponse(
    const base::DictValue& response) {
  const base::DictValue* answers = response.FindDict("answers");
  const base::DictValue* answer =
      answers ? answers->FindDict(kDestinationQuestion) : nullptr;
  return answer ? ChoiceAnswer(*answer, std::nullopt) : std::nullopt;
}

// Accepts the JSON object alone or inside a Markdown code fence.
std::optional<base::DictValue> ReadAnswerDict(const std::string& content) {
  std::string trimmed;
  base::TrimWhitespaceASCII(content, base::TRIM_ALL, &trimmed);
  if (trimmed.starts_with("```")) {
    const size_t newline = trimmed.find('\n');
    if (newline == std::string::npos) {
      return std::nullopt;
    }
    trimmed = trimmed.substr(newline + 1);
    const size_t fence = trimmed.rfind("```");
    if (fence != std::string::npos) {
      trimmed = trimmed.substr(0, fence);
    }
    base::TrimWhitespaceASCII(trimmed, base::TRIM_ALL, &trimmed);
  }
  return base::JSONReader::ReadDict(trimmed, base::JSON_PARSE_RFC);
}

std::optional<std::u16string> ValidTaskName(const std::string& name) {
  std::string trimmed;
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &trimmed);
  base::TrimString(trimmed, "\"'`", &trimmed);
  base::TrimWhitespaceASCII(trimmed, base::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::u16string title = base::UTF8ToUTF16(trimmed);
  if (title.size() > kMaxGeneratedTaskNameLength) {
    title.resize(kMaxGeneratedTaskNameLength);
    base::TrimWhitespace(title, base::TRIM_TRAILING, &title);
  }
  return title;
}

}  // namespace

std::optional<std::string> BuildClassificationRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  if (model.empty() || context.title.empty() ||
      !ValidTaskOptions(task_options)) {
    return std::nullopt;
  }
  base::DictValue description;
  description.Set("title", context.title);
  description.Set("url", context.url);
  description.Set("source", context.source_description);
  if (!context.excerpt.empty()) {
    description.Set("excerpt", context.excerpt);
  }
  if (!context.manual_placement.empty()) {
    description.Set("manual_placement", context.manual_placement);
  }
  base::ListValue tasks;
  for (const auto& [key, name] : task_options) {
    base::DictValue task;
    task.Set("key", key);
    task.Set("name", name);
    tasks.Append(std::move(task));
  }
  description.Set("tasks", std::move(tasks));

  base::DictValue system_message;
  system_message.Set("role", "system");
  system_message.Set("content", kSystemPrompt);
  base::DictValue user_message;
  user_message.Set("role", "user");
  user_message.Set("content", base::WriteJson(description).value_or("{}"));
  base::ListValue messages;
  messages.Append(std::move(system_message));
  messages.Append(std::move(user_message));
  base::DictValue body;
  body.Set("model", model);
  body.Set("messages", std::move(messages));
  body.Set("max_tokens", 200);
  body.Set("temperature", 0);
  return base::WriteJson(body);
}

std::optional<ClassificationAnswer> ParseClassificationResponse(
    const std::string& body) {
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const std::string* content = FirstMessageContent(*parsed);
  if (!content) {
    return std::nullopt;
  }
  auto answer_dict = ReadAnswerDict(*content);
  if (!answer_dict) {
    return std::nullopt;
  }
  const std::string* decision = answer_dict->FindString("decision");
  std::optional<double> confidence = answer_dict->FindDouble("confidence");
  if (!decision || !confidence || !ValidConfidence(*confidence)) {
    return std::nullopt;
  }
  ClassificationAnswer answer;
  answer.confidence = *confidence;
  if (*decision == "leave") {
    answer.choice = kScratchpadOption;
  } else if (*decision == "new_task") {
    answer.choice = kNewTaskOption;
    if (const std::string* name = answer_dict->FindString("name")) {
      answer.task_name = ValidTaskName(*name);
    }
  } else if (*decision == "move") {
    const std::string* target = answer_dict->FindString("target");
    if (!target || target->empty() || *target == kNewTaskOption ||
        *target == kScratchpadOption) {
      return std::nullopt;
    }
    answer.choice = *target;
  } else {
    return std::nullopt;
  }
  return answer;
}

std::optional<std::string> BuildDecisionRequestBody(
    DecisionFormat format,
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options) {
  if (format == DecisionFormat::kChat) {
    return BuildClassificationRequestBody(model, context, task_options);
  }
  if (model.empty() || context.title.empty() ||
      !ValidTaskOptions(task_options) ||
      task_options.size() + 2 > kMaxDecisionChoices) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? BuildDecisionsRequestBody(model, context, task_options)
             : BuildSystemOneRequestBody(model, context, task_options);
}

std::optional<ClassificationAnswer> ParseDecisionResponse(
    DecisionFormat format,
    const std::string& body) {
  if (format == DecisionFormat::kChat) {
    return ParseClassificationResponse(body);
  }
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? ParseDecisionsResponse(*parsed)
             : ParseSystemOneResponse(*parsed);
}

std::optional<std::string> BuildNamingRequestBody(
    const std::string& model,
    const BranchContext& context) {
  if (model.empty() || context.title.empty()) {
    return std::nullopt;
  }
  base::DictValue system_message;
  system_message.Set("role", "system");
  system_message.Set("content", kNamingPrompt);
  base::DictValue user_message;
  user_message.Set("role", "user");
  user_message.Set("content", DescribeBranch(context));
  base::ListValue messages;
  messages.Append(std::move(system_message));
  messages.Append(std::move(user_message));
  base::DictValue body;
  body.Set("model", model);
  body.Set("messages", std::move(messages));
  body.Set("max_tokens", 32);
  body.Set("temperature", 0);
  return base::WriteJson(body);
}

std::optional<std::u16string> ParseNamingResponse(const std::string& body) {
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  const std::string* content = parsed ? FirstMessageContent(*parsed) : nullptr;
  if (!content) {
    return std::nullopt;
  }
  std::string trimmed;
  base::TrimWhitespaceASCII(*content, base::TRIM_ALL, &trimmed);
  return ValidTaskName(trimmed.substr(0, trimmed.find('\n')));
}

std::optional<std::vector<std::u16string>> ParseModelList(
    const std::string& body) {
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const base::ListValue* data = parsed->FindList("data");
  if (!data) {
    return std::nullopt;
  }
  std::vector<std::u16string> models;
  for (const base::Value& item : *data) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* id = entry ? entry->FindString("id") : nullptr;
    if (!id || id->empty()) {
      return std::nullopt;
    }
    models.push_back(base::UTF8ToUTF16(*id));
  }
  return models;
}

}  // namespace uw
