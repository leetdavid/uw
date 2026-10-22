// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_
#define UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "uw/components/agent/agent_protocol.h"

namespace uw {

// What the model sees of a chat. Every tool result is kept in full, but the
// model gets one short line for the results it no longer needs, and the
// recall tool brings any of them back. Each request only adds to the one
// before it, except where a result shrinks, so a provider's cache serves the
// rest. See docs/plans/agent-chats.md#context.
class AgentContext {
 public:
  AgentContext();
  AgentContext(const AgentContext&) = delete;
  AgentContext& operator=(const AgentContext&) = delete;
  ~AgentContext();

  // A context saved by ToValue(), or null when `saved` is not one.
  static std::unique_ptr<AgentContext> FromValue(const base::DictValue& saved);
  base::DictValue ToValue() const;

  void AddUser(std::string_view text);
  // A message with `images`, each a data: URL. They are numbered on from the
  // chat's earlier images.
  void AddUser(std::string_view text, const std::vector<std::string>& images);
  // Shows the model the images it recalled by `numbers`, in a message of
  // their own.
  void ShowImages(const std::vector<int>& numbers);
  void AddAssistant(const ModelTurn& turn);
  // Records what a tool answered to the call `call_id` and returns its step
  // number. A reading of a tab shortens the older readings of that tab.
  int AddResult(std::string_view call_id,
                std::string_view tool,
                const base::DictValue& args,
                const base::DictValue& result);
  // The step or image `args` names, as the recall tool finds it.
  base::DictValue Recall(const base::DictValue& args) const;
  // What the recall tool answers to `args`: Recall(), unless it would take
  // the results since the user's latest message past half of `max_length`,
  // which Request() would shrink again.
  base::DictValue RecallWithin(const base::DictValue& args,
                               size_t max_length) const;
  // Lists `skills`, one per line, in the instructions every request starts
  // with. Empty for none.
  void SetSkills(std::string_view skills);
  // Says `connectors`, as ConnectorsForInstructions() writes them, in the
  // instructions. Empty for none.
  void SetConnectors(std::string_view connectors);
  // Tools every request offers beside uw's own: the connector tool and the
  // tools of each connector the model loaded.
  void SetExtraTools(base::ListValue tools);
  // A connector the model loaded, whose tools it keeps for the rest of the
  // chat.
  void LoadConnector(std::string_view name);
  const std::set<std::string>& loaded_connectors() const {
    return loaded_connectors_;
  }
  // A connector's tool the user allowed for the rest of the chat.
  void AllowForChat(std::string_view connector, std::string_view tool);
  bool AllowedForChat(std::string_view connector, std::string_view tool) const;
  // A connector the user turned off in this chat.
  void SetConnectorOn(std::string_view name, bool on);
  bool ConnectorOn(std::string_view name) const;
  const std::set<std::string>& connectors_off() const {
    return connectors_off_;
  }
  // Images a connector's tool answered with, shown to the model in a message
  // of their own, numbered with the chat's other images.
  void AddToolImages(std::string_view tool,
                     const std::vector<std::string>& images);
  // Answers each tool call of the model's last turn that has no result yet
  // with `error`. A model refuses a request with an unanswered call.
  void CloseOpenCalls(std::string_view error);

  // What the command check sees of the chat: the user's messages and the
  // agent's tool calls before the latest call `call_id`, oldest first, as lines
  // such as `User: ...` and `Agent: run {"command":"ls"}`. Never a tool's
  // answer or an image.
  std::vector<std::string> UserAndActions(std::string_view call_id) const;

  // The request for `model`. Above `max_length` characters, each image
  // counted as kImageLength, it shrinks the oldest results and images until
  // the request is half that long, those since the user's latest message last
  // and only as far as `max_length`.
  std::optional<std::string> Request(std::string_view model, size_t max_length);

  // What an image counts for in a request's length: about as many tokens as
  // a screenshot costs.
  static constexpr size_t kImageLength = 6000;

 private:
  struct Result {
    int step = 0;
    std::string tool;
    base::DictValue args;
    base::DictValue result;
    // Where the result sits in `messages_`.
    size_t message = 0;
    bool shortened = false;
  };

  struct Image {
    int number = 0;
    std::string url;
    // Where it sits: a message, and a part of that message's content.
    size_t message = 0;
    size_t part = 0;
    bool shortened = false;
  };

  static bool CanShorten(const Result& result);
  // The instructions, with the skills and connectors.
  void UpdateInstructions();
  std::optional<std::string> Body(std::string_view model) const;
  size_t Length(const std::string& body) const;
  // Where the user's latest message sits in `messages_`.
  size_t ReplyStart() const;
  // Shortens the oldest long result or image before `end`, or returns false
  // when there is none.
  bool ShortenOldestBefore(size_t end);
  void Shorten(Result& result);
  void Shorten(Image& image);
  // Drops what the provider kept with the turns after `message`: a provider
  // that checks it against the conversation before it refuses it once that
  // has changed.
  void DropKeptAfter(size_t message);
  const Image* FindImage(int number) const;

  base::ListValue messages_;
  std::string skills_;
  std::string connectors_;
  base::ListValue extra_tools_;
  std::set<std::string> loaded_connectors_;
  // "connector/tool".
  std::set<std::string> allowed_for_chat_;
  std::set<std::string> connectors_off_;
  std::vector<Result> results_;
  // A recalled image appears again under its own number.
  std::vector<Image> images_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_
