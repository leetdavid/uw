// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace uw {

const char kNewTaskOption[] = "new_task";
const char kScratchpadOption[] = "scratchpad";
const char kNoTaskOption[] = "none";
const size_t kMaxGeneratedTaskNameLength = 80;
const size_t kMaxExcerptLength = 2000;

namespace {

constexpr char kSystemPrompt[] =
    "You organize browser tabs into browsing tasks. The user message "
    "describes one tab with its title, URL, source-tab relationship, an "
    "optional readable-text excerpt, and the user's tasks.\n"
    "Respond with only a JSON object in this exact shape:\n"
    "{\"task\": \"<task key>\" | \"none\", \"confidence\": <number 0..1>, "
    "\"new_task\": <number 0..1>, \"name\": \"<short task name>\"}\n"
    "- \"task\": the key of the task the tab clearly continues, or \"none\" "
    "when it fits none of them.\n"
    "- \"confidence\": how sure you are of \"task\".\n"
    "- \"new_task\": the probability that the tab starts a piece of work or "
    "research of its own that the user is likely to continue in more tabs. A "
    "one-off scores low: a quick lookup, a sign-in or verification page, a "
    "home page, a feed, or a dashboard.\n"
    "- \"name\": a short action-oriented phrase of at most five words that "
    "names that piece of work.";

// Some chat models think before they answer, and the provider counts that
// thinking against the limit. A small limit leaves them no room to answer.
constexpr int kMaxChatAnswerTokens = 4096;

// A decision model answers two choice questions. The first asks which task
// the branch belongs to, or none. The second asks what a branch outside every
// task is. Asked as one question, the two ways of being outside a task split
// the model's vote, and neither ever looked like a confident answer.
constexpr char kDestinationQuestion[] = "destination";
constexpr char kOtherwiseQuestion[] = "otherwise";
constexpr size_t kMaxDecisionChoices = 255;
constexpr char kDestinationInstructions[] =
    "Decide which of the user's browsing tasks this tab belongs to. Choose a "
    "task only when the tab clearly continues the work that task names. "
    "Choose none when it fits none of them.";
constexpr char kNoTaskDescription[] = "The tab belongs to none of these tasks.";
constexpr char kOtherwiseInstructions[] =
    "Suppose this tab belongs to none of the user's existing browsing tasks. "
    "Decide what it is.";
constexpr char kNewTaskDescription[] =
    "The tab starts a piece of work or research of its own that the user is "
    "likely to continue in more tabs.";
constexpr char kScratchpadDescription[] =
    "The tab is a one-off: a quick lookup, a sign-in or verification page, a "
    "home page, a feed, or a dashboard.";
constexpr char kNamingPrompt[] =
    "Name the browsing task that this tab starts. Reply with only the name: a "
    "short, action-oriented phrase of at most five words, with no quotes.";

bool ValidConfidence(double value) {
  return value >= 0.0 && value <= 1.0;
}

using Options = std::vector<std::pair<std::string, std::string>>;

bool Reserved(const std::string& key) {
  return key == kNewTaskOption || key == kScratchpadOption ||
         key == kNoTaskOption;
}

bool ValidTaskOptions(const Options& task_options) {
  for (const auto& [key, name] : task_options) {
    if (key.empty() || Reserved(key)) {
      return false;
    }
  }
  return true;
}

Options DestinationOptions(Options task_options) {
  task_options.emplace_back(kNoTaskOption, kNoTaskDescription);
  return task_options;
}

Options OtherwiseOptions() {
  return {{kNewTaskOption, kNewTaskDescription},
          {kScratchpadOption, kScratchpadDescription}};
}

std::string DescribeBranch(const BranchContext& context) {
  std::string text = "Title: " + context.title + "\nURL: " + context.url +
                     "\nSource: " + context.source_description;
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
  const base::DictValue* message =
      choice ? choice->FindDict("message") : nullptr;
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

// The probability a choice answer gives `option`. The Decisions API lists
// them as {value, probability} pairs, System One as a dictionary.
std::optional<double> Probability(const base::DictValue& answer,
                                  std::string_view option) {
  if (const base::DictValue* table = answer.FindDict("probabilities")) {
    return table->FindDouble(option);
  }
  const base::ListValue* listed = answer.FindList("probabilities");
  if (!listed) {
    return std::nullopt;
  }
  for (const base::Value& item : *listed) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* value = entry ? entry->FindString("value") : nullptr;
    if (value && *value == option) {
      return entry->FindDouble("probability");
    }
  }
  return std::nullopt;
}

std::optional<double> ChosenProbability(const base::DictValue& answer) {
  const std::string* choice = answer.FindString("choice");
  return choice ? Probability(answer, *choice) : std::nullopt;
}

// Joins the answers to the two questions into one decision. `destination` is
// read only when the request listed tasks. `listed` lets a missing confidence
// fall back on the probability the answer lists for its choice.
std::optional<ClassificationAnswer> JoinAnswers(
    const base::DictValue* destination,
    const base::DictValue* otherwise,
    bool has_tasks,
    bool listed) {
  const auto read = [listed](const base::DictValue* answer) {
    return answer ? ChoiceAnswer(*answer, listed ? ChosenProbability(*answer)
                                                 : std::nullopt)
                  : std::nullopt;
  };
  double outside = 1.0;
  if (has_tasks) {
    std::optional<ClassificationAnswer> task = read(destination);
    if (!task || task->choice == kNewTaskOption ||
        task->choice == kScratchpadOption) {
      return std::nullopt;
    }
    if (task->choice != kNoTaskOption) {
      return task;
    }
    outside = task->confidence;
  }
  std::optional<ClassificationAnswer> kind = read(otherwise);
  if (!kind ||
      (kind->choice != kNewTaskOption && kind->choice != kScratchpadOption)) {
    return std::nullopt;
  }
  const double new_task =
      Probability(*otherwise, kNewTaskOption)
          .value_or(kind->choice == kNewTaskOption ? kind->confidence
                                                   : 1.0 - kind->confidence);
  if (!ValidConfidence(new_task)) {
    return std::nullopt;
  }
  kind->confidence = outside;
  kind->new_task = new_task;
  return kind;
}

std::optional<std::string> BuildDecisionsRequestBody(
    const std::string& model,
    const BranchContext& context,
    const Options& task_options) {
  base::ListValue questions;
  const auto ask = [&questions](const char* name, const char* instructions,
                                const Options& options) {
    base::ListValue choices;
    for (const auto& [value, description] : options) {
      base::DictValue choice;
      choice.Set("value", value);
      choice.Set("description", description);
      choices.Append(std::move(choice));
    }
    base::DictValue question;
    question.Set("type", "choice");
    question.Set("name", name);
    question.Set("instructions", instructions);
    question.Set("choices", std::move(choices));
    questions.Append(std::move(question));
  };
  if (!task_options.empty()) {
    ask(kDestinationQuestion, kDestinationInstructions,
        DestinationOptions(task_options));
  }
  ask(kOtherwiseQuestion, kOtherwiseInstructions, OtherwiseOptions());
  base::DictValue body;
  body.Set("model", model);
  body.Set("input", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<std::string> BuildSystemOneRequestBody(
    const std::string& model,
    const BranchContext& context,
    const Options& task_options) {
  base::DictValue questions;
  const auto ask = [&questions](const char* name, const char* instructions,
                                const Options& options) {
    base::DictValue criteria;
    for (const auto& [key, description] : options) {
      criteria.Set(key, description);
    }
    base::DictValue question;
    question.Set("type", "choice");
    question.Set("instructions", instructions);
    question.Set("criteria", std::move(criteria));
    questions.Set(name, std::move(question));
  };
  if (!task_options.empty()) {
    ask(kDestinationQuestion, kDestinationInstructions,
        DestinationOptions(task_options));
  }
  ask(kOtherwiseQuestion, kOtherwiseInstructions, OtherwiseOptions());
  base::DictValue body;
  body.Set("model", model);
  body.Set("state", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<ClassificationAnswer> ParseDecisionsResponse(
    const base::DictValue& response,
    bool has_tasks) {
  const base::ListValue* answers = response.FindList("answers");
  if (!answers) {
    return std::nullopt;
  }
  const auto find = [answers](std::string_view question) {
    for (const base::Value& item : *answers) {
      const base::DictValue* answer = item.GetIfDict();
      const std::string* name = answer ? answer->FindString("name") : nullptr;
      if (name && *name == question) {
        return answer;
      }
    }
    return static_cast<const base::DictValue*>(nullptr);
  };
  return JoinAnswers(find(kDestinationQuestion), find(kOtherwiseQuestion),
                     has_tasks, /*listed=*/true);
}

std::optional<ClassificationAnswer> ParseSystemOneResponse(
    const base::DictValue& response,
    bool has_tasks) {
  const base::DictValue* answers = response.FindDict("answers");
  if (!answers) {
    return std::nullopt;
  }
  return JoinAnswers(answers->FindDict(kDestinationQuestion),
                     answers->FindDict(kOtherwiseQuestion), has_tasks,
                     /*listed=*/false);
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
  body.Set("max_tokens", kMaxChatAnswerTokens);
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
  const std::string* task = answer_dict->FindString("task");
  std::optional<double> confidence = answer_dict->FindDouble("confidence");
  if (!task || task->empty() || *task == kNewTaskOption ||
      *task == kScratchpadOption || !confidence ||
      !ValidConfidence(*confidence)) {
    return std::nullopt;
  }
  ClassificationAnswer answer;
  answer.confidence = *confidence;
  if (*task != kNoTaskOption) {
    answer.choice = *task;
    return answer;
  }
  std::optional<double> new_task = answer_dict->FindDouble("new_task");
  if (!new_task || !ValidConfidence(*new_task)) {
    return std::nullopt;
  }
  answer.new_task = *new_task;
  if (*new_task < 0.5) {
    answer.choice = kScratchpadOption;
    return answer;
  }
  answer.choice = kNewTaskOption;
  if (const std::string* name = answer_dict->FindString("name")) {
    answer.task_name = ValidTaskName(*name);
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
      task_options.size() + 1 > kMaxDecisionChoices) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? BuildDecisionsRequestBody(model, context, task_options)
             : BuildSystemOneRequestBody(model, context, task_options);
}

std::optional<ClassificationAnswer> ParseDecisionResponse(
    DecisionFormat format,
    const std::string& body,
    bool has_tasks) {
  if (format == DecisionFormat::kChat) {
    return ParseClassificationResponse(body);
  }
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? ParseDecisionsResponse(*parsed, has_tasks)
             : ParseSystemOneResponse(*parsed, has_tasks);
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
  body.Set("max_tokens", kMaxChatAnswerTokens);
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
  const base::ListValue* listed = parsed->FindList("data");
  const char* id_key = "id";
  if (!listed) {
    listed = parsed->FindList("models");
    id_key = "name";
  }
  if (!listed) {
    return std::nullopt;
  }
  std::vector<std::u16string> models;
  for (const base::Value& item : *listed) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* id = entry ? entry->FindString(id_key) : nullptr;
    if (!id || id->empty()) {
      return std::nullopt;
    }
    models.push_back(base::UTF8ToUTF16(*id));
  }
  return models;
}

}  // namespace uw
