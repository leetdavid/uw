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

bool ValidConfidence(double value) {
  return value >= 0.0 && value <= 1.0;
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
  if (model.empty() || context.title.empty()) {
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
    if (key.empty() || key == kNewTaskOption || key == kScratchpadOption) {
      return std::nullopt;
    }
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
  const base::ListValue* choices = parsed->FindList("choices");
  if (!choices || choices->empty()) {
    return std::nullopt;
  }
  const base::DictValue* choice = (*choices)[0].GetIfDict();
  const base::DictValue* message = choice ? choice->FindDict("message") : nullptr;
  const std::string* content = message ? message->FindString("content") : nullptr;
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
