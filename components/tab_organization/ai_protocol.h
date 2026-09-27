// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uw {

// Pure request/response handling for the single classification call behind
// automatic organization. The call runs against an OpenAI-compatible
// chat-completions endpoint, including Ollama's. No networking, prefs, or
// credentials here; browser integration owns transport. All functions
// validate strictly and reject malformed input so a bad provider response can
// never move a tab.

// Option key meaning "create a new root-level task".
extern const char kNewTaskOption[];
// Option key meaning "leave the branch in Scratchpad".
extern const char kScratchpadOption[];
// Provisional title for a task created without a valid generated name.
extern const char16_t kAutomaticTaskProvisionalTitle[];
// Maximum accepted length of a generated task name, in characters.
extern const size_t kMaxGeneratedTaskNameLength;
// Maximum accepted length of the readable-page-text excerpt, in characters.
extern const size_t kMaxExcerptLength;

// Metadata sent for one tab branch. `excerpt` is short readable page text.
// `manual_placement` is empty or describes the user's manual placement of the
// branch. `title` must be non-empty.
struct BranchContext {
  std::string title;
  std::string url;
  std::string source_description;
  std::string excerpt;
  std::string manual_placement;
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
  // A task option key, or kNewTaskOption, or kScratchpadOption.
  std::string choice;
  double confidence = 0.0;
  // Valid generated name for kNewTaskOption; absent leaves the provisional
  // title.
  std::optional<std::u16string> task_name;
};

// Parses and validates the placement decision out of a chat-completions
// response body. The assistant content must be one JSON object, optionally
// wrapped in a Markdown code fence. Anything ambiguous or malformed is
// rejected.
std::optional<ClassificationAnswer> ParseClassificationResponse(
    const std::string& body);

// Parses the model ids out of a chat-completions model-list response body.
// An empty result means the endpoint lists no models.
std::optional<std::vector<std::u16string>> ParseModelList(
    const std::string& body);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
