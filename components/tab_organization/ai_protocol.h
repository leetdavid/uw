// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uw {

// Pure request/response handling for the placement call behind automatic
// organization. The call asks two things about one tab branch: which of the
// user's tasks it belongs to, if any, and what a branch outside every task
// is. A decision model can also be asked whether the task the branch is in
// needs a new name. No networking, prefs, or credentials here; browser
// integration owns transport. All functions validate strictly and reject
// malformed input so a bad provider response can never move a tab.

// A branch outside every task that starts a task of its own.
extern const char kNewTaskOption[];
// A branch outside every task that is a one-off for the Scratchpad.
extern const char kScratchpadOption[];
// What a model answers when the branch belongs to none of the tasks.
extern const char kNoTaskOption[];
// Maximum accepted length of a generated task name, in characters.
extern const size_t kMaxGeneratedTaskNameLength;
// Maximum accepted length of the readable-page-text excerpt, in characters.
extern const size_t kMaxExcerptLength;
// How many other tab titles a naming request or a name check lists.
extern const size_t kMaxNamingTitles;

// Metadata sent for one tab branch. `excerpt` is short readable page text.
// `title` must be non-empty.
struct BranchContext {
  std::string title;
  std::string url;
  std::string source_description;
  std::string excerpt;
};

// Builds an OpenAI-compatible chat-completions request body. `task_options`
// maps stable option keys to the current task names; the answer references
// those keys. Reserved option keys and empty keys or model names are rejected.
std::optional<std::string> BuildClassificationRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options);

// One validated placement decision from a classification response.
struct ClassificationAnswer {
  // The task the branch belongs to, as a task option key. For a branch that
  // belongs to no task: kNewTaskOption when the model finds it more likely
  // than not that it starts a task of its own, kScratchpadOption otherwise.
  std::string choice;
  // How sure the model is of that task, or that the branch belongs to none.
  double confidence = 0.0;
  // For a branch that belongs to no task, the probability that it starts a
  // task of its own.
  double new_task = 0.0;
  // Valid generated name for kNewTaskOption, when the format carries one.
  std::optional<std::u16string> task_name;
  // The probability that the task the branch is in needs a new name. Zero
  // when the request carried no name check.
  double new_name = 0.0;
};

// The name check: the task a branch is in, for the question whether that
// task needs a new name. `other_titles` are the titles of its other tabs;
// only the first kMaxNamingTitles are sent.
struct NameCheck {
  std::string name;
  std::vector<std::string> other_titles;
};

// Parses and validates the placement decision out of a chat-completions
// response body. The assistant content must be one JSON object, optionally
// wrapped in a Markdown code fence. Anything ambiguous or malformed is
// rejected.
std::optional<ClassificationAnswer> ParseClassificationResponse(
    const std::string& body);

// Wire formats a placement decision can use.
enum class DecisionFormat {
  // OpenAI-compatible chat completions. The answer can carry a task name.
  kChat,
  // OpenAI Decisions API: POST <base>/decisions with choice questions.
  kDecisions,
  // TypeSafe System One (Jev): POST <base>/systemone with choice questions.
  kSystemOne,
};

// Builds the request body for one placement decision in `format`. Rejects the
// same input as BuildClassificationRequestBody, plus task lists too long for
// one choice question. With no tasks, a decision model is asked only what a
// branch outside every task is. `name_check` adds the question whether the
// branch's task needs a new name. A chat request never carries it.
std::optional<std::string> BuildDecisionRequestBody(
    DecisionFormat format,
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::pair<std::string, std::string>>& task_options,
    const std::optional<NameCheck>& name_check = std::nullopt);

// Parses and validates one placement decision out of a `format` response.
// `has_tasks` says whether the request listed any task: a decision model's
// answer must then say which one the branch belongs to, or none. Decision
// models never name tasks, so `task_name` stays empty for them. A refusal, or
// an answer without a usable confidence, is rejected. The answer to the name
// check is read only when `has_name_check` says the request carried one. A
// missing or unusable one only leaves `new_name` at zero.
std::optional<ClassificationAnswer> ParseDecisionResponse(
    DecisionFormat format,
    const std::string& body,
    bool has_tasks,
    bool has_name_check = false);

// Builds an OpenAI-compatible chat-completions request asking for a short
// name for a browsing task. `context` is the tab that starts the task, or
// the tab that just changed in a task that needs a name. `other_titles` are
// the titles of the other tabs in that task; only the first kMaxNamingTitles
// are sent.
std::optional<std::string> BuildNamingRequestBody(
    const std::string& model,
    const BranchContext& context,
    const std::vector<std::string>& other_titles = {});

// Extracts the generated task name from a chat-completions response body.
std::optional<std::u16string> ParseNamingResponse(const std::string& body);

// Parses the model ids out of a model-list response body. OpenAI-compatible
// endpoints list them as `data[].id`, TypeSafe as `models[].name`. An empty
// result means the endpoint lists no models.
std::optional<std::vector<std::u16string>> ParseModelList(
    const std::string& body);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
