// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace uw {

// Pure request/response handling for the two remote providers behind
// automatic organization. No networking, prefs, or credentials here; browser
// integration owns transport. All functions validate strictly and reject
// malformed input so a bad provider response can never move a tab.

// Default Jev evaluation endpoint and model.
extern const char kJevDefaultEndpoint[];
extern const char kJevModel[];
// Option key meaning "create a new root-level task".
extern const char kJevNewTaskOption[];
// Option key meaning "leave the branch in Scratchpad".
extern const char kJevScratchpadOption[];
// Provisional title for an automatically created task.
extern const char16_t kAutomaticTaskProvisionalTitle[];
// Maximum accepted length for a generated task name.
extern const size_t kMaxGeneratedTaskNameLength;

struct JevChoiceAnswer {
  std::string choice;
  std::map<std::string, double> probabilities;
  double confidence = 0.0;
};

// Builds a Jev systemone request body. `state` carries tab title, URL,
// source-tab relationship, and current task names. `task_options` maps stable
// option keys to human-readable descriptions; the caller must include the
// new-task and Scratchpad options.
std::optional<std::string> BuildJevRequestBody(
    const std::string& title,
    const std::string& url,
    const std::string& source_description,
    const std::vector<std::pair<std::string, std::string>>& task_options);

// Parses one Choice answer out of a Jev systemone response body.
std::optional<JevChoiceAnswer> ParseJevChoiceResponse(
    const std::string& body,
    const std::string& question_id);

// Builds an OpenAI-compatible chat-completions request body for task naming.
std::optional<std::string> BuildNamingRequestBody(
    const std::string& model,
    const std::string& prompt);

// Extracts, trims, and caps the generated task name. Rejects empty content.
std::optional<std::u16string> ParseNamingResponse(const std::string& body);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_AI_PROTOCOL_H_
