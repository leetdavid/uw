// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/tab_organization/ai_protocol.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace uw {

const char kNewTaskOption[] = "new_task";
const char kScratchpadOption[] = "scratchpad";
const char kNoTaskOption[] = "none";
const size_t kMaxGeneratedTaskNameLength = 80;
const size_t kMaxExcerptLength = 2000;
const size_t kMaxNamingTitles = 20;

namespace {

constexpr char kSystemPrompt[] =
    "You organize browser tabs into browsing tasks. The user message "
    "describes one tab with its title, URL, source-tab relationship, an "
    "optional readable-text excerpt or a terminal's folder, and the user's "
    "tasks.\n"
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
// Asked about a branch in a task with other tabs. The branch leaves the task
// only as a different piece of work from those tabs. One that is the same
// piece of work, and yet fits no task name, shows that the task has outgrown
// a name uw gave it.
constexpr char kNameQuestion[] = "name";
constexpr char kSameWorkOption[] = "same";
constexpr char kOtherWorkOption[] = "different";
constexpr char kSameWorkDescription[] =
    "This tab continues the piece of work those tabs share.";
constexpr char kOtherWorkDescription[] =
    "This tab is a different piece of work from those tabs.";
// Asked about every tab. uw never makes a hub into a task. A search page
// scores like a one-off on the question before, so that one cannot tell the
// two apart.
constexpr char kKindQuestion[] = "kind";
constexpr char kHubOption[] = "hub";
constexpr char kSubjectOption[] = "subject";
constexpr char kKindInstructions[] = "Decide what kind of page this tab shows.";
constexpr char kHubDescription[] =
    "The tab is a place the user comes back to for many things: a home page, "
    "a feed, an inbox, a dashboard, or a list of items.";
constexpr char kSubjectDescription[] =
    "The tab is about one subject: an article, a search for something, a "
    "document, a product, a video, or a discussion.";
constexpr char kNamingPrompt[] =
    "Name the browsing task that this tab starts. Reply with only the name: a "
    "short, action-oriented phrase of at most five words, with no quotes.";
constexpr char kTaskNamingPrompt[] =
    "Name the browsing task that these tabs belong to. Reply with only the "
    "name: a short, action-oriented phrase of at most five words, with no "
    "quotes.";
constexpr char kTidyPrompt[] =
    "File the user's loose browser tabs into their browsing tasks. A browsing "
    "task is the immediate activity the user pursues with a set of tabs, such "
    "as \"Plan Lisbon trip\". For each loose tab, give the number of the task "
    "it belongs to, or the name of a new task it starts together with other "
    "loose tabs, or neither for a one-off page. A new task needs at least two "
    "tabs, and all of them carry exactly the same name: a short, "
    "action-oriented phrase of at most five words. Mark a hub with "
    "\"hub\": true: a page the user comes back to for many things, such as a "
    "home page, a feed, an inbox, or a dashboard. A hub joins no task. Reply "
    "with only JSON, one entry per loose tab, like "
    "{\"tabs\":[{\"tab\":1,\"task\":2},{\"tab\":2,\"new_task\":\"Choose a "
    "coffee grinder\"},{\"tab\":3,\"new_task\":\"Choose a coffee "
    "grinder\"},{\"tab\":4},{\"tab\":5,\"hub\":true}]}.";

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

Options NameOptions() {
  return {{kSameWorkOption, kSameWorkDescription},
          {kOtherWorkOption, kOtherWorkDescription}};
}

Options KindOptions() {
  return {{kHubOption, kHubDescription}, {kSubjectOption, kSubjectDescription}};
}

std::string ListTitles(const std::vector<std::string>& titles) {
  std::string list;
  for (size_t i = 0; i < std::min(titles.size(), kMaxNamingTitles); ++i) {
    list += "\n- " + titles[i];
  }
  return list;
}

std::string NameInstructions(const NameCheck& check) {
  return "The tab sits in a group with these tabs:" +
         ListTitles(check.other_titles) +
         "\nDecide whether this tab is part of the same piece of work as "
         "those tabs.";
}

std::string DescribeBranch(const BranchContext& context) {
  std::string text = "Title: " + context.title + "\nURL: " + context.url +
                     "\nSource: " + context.source_description;
  if (!context.excerpt.empty()) {
    text += "\nPage excerpt: " + context.excerpt;
  }
  if (!context.folder.empty()) {
    text += "\nFolder: " + context.folder;
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

// `listed` lets a missing confidence fall back on the probability the answer
// lists for its choice.
std::optional<ClassificationAnswer> ReadChoice(const base::DictValue* answer,
                                               bool listed) {
  return answer ? ChoiceAnswer(*answer, listed ? ChosenProbability(*answer)
                                               : std::nullopt)
                : std::nullopt;
}

// The probability a two-option answer gives `option`.
double ProbabilityOf(const base::DictValue& answer,
                     const ClassificationAnswer& read,
                     std::string_view option) {
  return Probability(answer, option)
      .value_or(read.choice == option ? read.confidence
                                      : 1.0 - read.confidence);
}

// The probability `answer` gives `option`, the first of its two options.
// Zero for a missing or unusable answer, so a bad one changes nothing.
double OptionProbability(const base::DictValue* answer,
                         bool listed,
                         std::string_view option,
                         std::string_view other) {
  const std::optional<ClassificationAnswer> read = ReadChoice(answer, listed);
  if (!read || (read->choice != option && read->choice != other)) {
    return 0.0;
  }
  const double probability = ProbabilityOf(*answer, *read, option);
  return ValidConfidence(probability) ? probability : 0.0;
}

// Joins the answers to the two placement questions into one decision.
// `destination` is read only when the request listed tasks.
std::optional<ClassificationAnswer> JoinPlacement(
    const base::DictValue* destination,
    const base::DictValue* otherwise,
    bool has_tasks,
    bool listed) {
  double outside = 1.0;
  if (has_tasks) {
    std::optional<ClassificationAnswer> task = ReadChoice(destination, listed);
    if (!task || task->choice == kNewTaskOption ||
        task->choice == kScratchpadOption) {
      return std::nullopt;
    }
    if (task->choice != kNoTaskOption) {
      return task;
    }
    outside = task->confidence;
  }
  std::optional<ClassificationAnswer> kind = ReadChoice(otherwise, listed);
  if (!kind ||
      (kind->choice != kNewTaskOption && kind->choice != kScratchpadOption)) {
    return std::nullopt;
  }
  const double new_task = ProbabilityOf(*otherwise, *kind, kNewTaskOption);
  if (!ValidConfidence(new_task)) {
    return std::nullopt;
  }
  kind->confidence = outside;
  kind->new_task = new_task;
  return kind;
}

std::optional<ClassificationAnswer> JoinAnswers(
    const base::DictValue* destination,
    const base::DictValue* otherwise,
    const base::DictValue* name,
    const base::DictValue* kind,
    bool has_tasks,
    bool listed) {
  std::optional<ClassificationAnswer> answer =
      JoinPlacement(destination, otherwise, has_tasks, listed);
  if (answer) {
    answer->same_work =
        OptionProbability(name, listed, kSameWorkOption, kOtherWorkOption);
    answer->hub = OptionProbability(kind, listed, kHubOption, kSubjectOption);
  }
  return answer;
}

std::optional<std::string> BuildDecisionsRequestBody(
    const std::string& model,
    const BranchContext& context,
    const Options& task_options,
    const std::optional<NameCheck>& name_check) {
  base::ListValue questions;
  const auto ask = [&questions](const char* name,
                                const std::string& instructions,
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
  if (name_check) {
    ask(kNameQuestion, NameInstructions(*name_check), NameOptions());
  }
  ask(kKindQuestion, kKindInstructions, KindOptions());
  base::DictValue body;
  body.Set("model", model);
  body.Set("input", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<std::string> BuildSystemOneRequestBody(
    const std::string& model,
    const BranchContext& context,
    const Options& task_options,
    const std::optional<NameCheck>& name_check) {
  base::DictValue questions;
  const auto ask = [&questions](const char* name,
                                const std::string& instructions,
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
  if (name_check) {
    ask(kNameQuestion, NameInstructions(*name_check), NameOptions());
  }
  ask(kKindQuestion, kKindInstructions, KindOptions());
  base::DictValue body;
  body.Set("model", model);
  body.Set("state", DescribeBranch(context));
  body.Set("questions", std::move(questions));
  return base::WriteJson(body);
}

std::optional<ClassificationAnswer> ParseDecisionsResponse(
    const base::DictValue& response,
    bool has_tasks,
    bool has_name_check) {
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
                     has_name_check ? find(kNameQuestion) : nullptr,
                     find(kKindQuestion), has_tasks, /*listed=*/true);
}

std::optional<ClassificationAnswer> ParseSystemOneResponse(
    const base::DictValue& response,
    bool has_tasks,
    bool has_name_check) {
  const base::DictValue* answers = response.FindDict("answers");
  if (!answers) {
    return std::nullopt;
  }
  return JoinAnswers(
      answers->FindDict(kDestinationQuestion),
      answers->FindDict(kOtherwiseQuestion),
      has_name_check ? answers->FindDict(kNameQuestion) : nullptr,
      answers->FindDict(kKindQuestion), has_tasks, /*listed=*/false);
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
  if (!context.folder.empty()) {
    description.Set("folder", context.folder);
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
    const std::vector<std::pair<std::string, std::string>>& task_options,
    const std::optional<NameCheck>& name_check) {
  if (format == DecisionFormat::kChat) {
    return BuildClassificationRequestBody(model, context, task_options);
  }
  if (model.empty() || context.title.empty() ||
      !ValidTaskOptions(task_options) ||
      task_options.size() + 1 > kMaxDecisionChoices) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? BuildDecisionsRequestBody(model, context, task_options,
                                         name_check)
             : BuildSystemOneRequestBody(model, context, task_options,
                                         name_check);
}

std::optional<ClassificationAnswer> ParseDecisionResponse(
    DecisionFormat format,
    const std::string& body,
    bool has_tasks,
    bool has_name_check) {
  if (format == DecisionFormat::kChat) {
    return ParseClassificationResponse(body);
  }
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  return format == DecisionFormat::kDecisions
             ? ParseDecisionsResponse(*parsed, has_tasks, has_name_check)
             : ParseSystemOneResponse(*parsed, has_tasks, has_name_check);
}

std::optional<std::string> BuildNamingRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::string>& other_titles) {
  if (model.empty() || context.title.empty()) {
    return std::nullopt;
  }
  std::string tabs = DescribeBranch(context);
  if (!other_titles.empty()) {
    tabs += "\nOther tabs in the task:" + ListTitles(other_titles);
  }
  base::DictValue system_message;
  system_message.Set("role", "system");
  system_message.Set("content",
                     other_titles.empty() ? kNamingPrompt : kTaskNamingPrompt);
  base::DictValue user_message;
  user_message.Set("role", "user");
  user_message.Set("content", std::move(tabs));
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

std::optional<std::string> BuildTidyRequestBody(
    const std::string& model,
    const std::vector<TidyTab>& tabs,
    const std::vector<TidyTask>& tasks) {
  if (model.empty() || tabs.empty()) {
    return std::nullopt;
  }
  std::string text = tasks.empty() ? "Tasks: none" : "Tasks:";
  for (size_t i = 0; i < tasks.size(); ++i) {
    text += "\n" + base::NumberToString(i + 1) + ". " + tasks[i].name +
            ListTitles(tasks[i].titles);
  }
  text += "\n\nLoose tabs:";
  for (size_t i = 0; i < tabs.size(); ++i) {
    const TidyTab& tab = tabs[i];
    if (tab.title.empty()) {
      return std::nullopt;
    }
    text += "\n" + base::NumberToString(i + 1) + ". Title: " + tab.title +
            "\nURL: " + tab.url;
    if (tab.parent) {
      text += "\nOpened from: tab " + base::NumberToString(tab.parent);
    }
    if (!tab.folder.empty()) {
      text += "\nFolder: " + tab.folder;
    }
  }
  base::DictValue system_message;
  system_message.Set("role", "system");
  system_message.Set("content", kTidyPrompt);
  base::DictValue user_message;
  user_message.Set("role", "user");
  user_message.Set("content", std::move(text));
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

std::optional<std::vector<TidyChoice>> ParseTidyResponse(
    const std::string& body,
    size_t tab_count,
    size_t task_count) {
  auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  const std::string* content = parsed ? FirstMessageContent(*parsed) : nullptr;
  std::optional<base::DictValue> answer =
      content ? ReadAnswerDict(*content) : std::nullopt;
  const base::ListValue* listed = answer ? answer->FindList("tabs") : nullptr;
  if (!listed) {
    return std::nullopt;
  }
  std::vector<TidyChoice> choices(tab_count);
  std::vector<bool> seen(tab_count);
  for (const base::Value& item : *listed) {
    const base::DictValue* entry = item.GetIfDict();
    const std::optional<int> tab = entry ? entry->FindInt("tab") : std::nullopt;
    if (!tab || *tab < 1 || static_cast<size_t>(*tab) > tab_count ||
        seen[*tab - 1]) {
      return std::nullopt;
    }
    seen[*tab - 1] = true;
    const std::optional<int> task = entry->FindInt("task");
    const std::string* new_task = entry->FindString("new_task");
    if ((task && new_task) ||
        (task && (*task < 1 || static_cast<size_t>(*task) > task_count))) {
      return std::nullopt;
    }
    if (entry->FindBool("hub").value_or(false)) {
      continue;
    }
    TidyChoice& choice = choices[*tab - 1];
    if (task) {
      choice.task = static_cast<size_t>(*task);
    } else if (new_task) {
      std::optional<std::u16string> name = ValidTaskName(*new_task);
      if (!name) {
        return std::nullopt;
      }
      choice.new_task = std::move(*name);
    }
  }
  return choices;
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

std::set<std::string> DecisionModelsIn(const std::string& body) {
  std::set<std::string> ids;
  const std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  const base::ListValue* listed = parsed ? parsed->FindList("data") : nullptr;
  if (!listed) {
    return ids;
  }
  for (const base::Value& item : *listed) {
    const base::DictValue* entry = item.GetIfDict();
    const std::string* id = entry ? entry->FindString("id") : nullptr;
    const std::string* type = entry ? entry->FindString("type") : nullptr;
    if (id && type && *type == "evaluation") {
      ids.insert(*id);
    }
  }
  return ids;
}

}  // namespace uw
