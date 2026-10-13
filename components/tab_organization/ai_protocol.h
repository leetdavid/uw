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
// is. A decision model is also asked what kind of page the tab shows, and can
// be asked whether the branch is the same piece of work as the other tabs of
// its task. No networking, prefs, or credentials here; browser
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
  // The probability that the branch is the same piece of work as the other
  // tabs of its task. Zero when the request carried no name check.
  double same_work = 0.0;
  // The probability that the tab shows a hub: a page the user comes back to
  // for many things, such as a home page, a feed, an inbox, or a dashboard.
  // Zero when the format has no such answer.
  double hub = 0.0;
};

// The name check: the question whether a branch is the same piece of work as
// the other tabs of its task. A branch that is, and yet fits no task name,
// shows that its task has outgrown the name. `other_titles` are the titles of
// those tabs; only the first kMaxNamingTitles are sent.
struct NameCheck {
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
// one choice question. With no tasks, a decision model is not asked which one
// the branch belongs to. It is always asked what kind of page the tab shows.
// `name_check` adds the question whether the branch is the same piece of work
// as its task's other tabs. A chat request never carries it.
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
// missing or unusable one only leaves `same_work` at zero, as a missing or
// unusable answer about the kind of page leaves `hub` at zero.
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
