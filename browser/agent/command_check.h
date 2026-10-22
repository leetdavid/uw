// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_COMMAND_CHECK_H_
#define UW_BROWSER_AGENT_COMMAND_CHECK_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/agent/command_check.h"

class Profile;

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

class ModelCatalog;

// One command check under way: to the decision model, or to the language
// model when there is none, or else to the chat's own model. A language model
// checks with thinking off where the catalog says how, and is asked again
// without that if its provider rejects it; one with no known way gets 90
// seconds. Destroying the check drops it. See docs/plans/agent-chats.md.
class CommandCheck {
 public:
  using Done = base::OnceCallback<void(std::optional<CommandCheckAnswer>)>;

  // Sends the check of `input`, with `chat_model` and `chat_key` for the
  // chat's own model. `done` gets what the answer says, or nothing for no
  // answer. Null when no model can take it.
  static std::unique_ptr<CommandCheck> Send(
      Profile& profile,
      const AiConnectionConfig& chat_model,
      const std::string& chat_key,
      const CommandCheckInput& input,
      Done done);

  CommandCheck(const CommandCheck&) = delete;
  CommandCheck& operator=(const CommandCheck&) = delete;
  ~CommandCheck();

 private:
  CommandCheck(Profile& profile, const CommandCheckInput& input, Done done);

  void SendLanguageCheck(const ModelCatalog* catalog);
  void SendToLanguageModel(const AiConnectionConfig& model,
                           const std::string& body,
                           base::TimeDelta timeout);
  void Start(std::unique_ptr<RemoteModelClient> client);
  void OnAnswer(RemoteModelClient::Outcome outcome);

  scoped_refptr<network::SharedURLLoaderFactory> loader_;
  const CommandCheckInput input_;
  Done done_;
  DecisionFormat format_ = DecisionFormat::kChat;
  // The language model and its request, as the check sends them when it
  // knows no way to turn thinking off.
  AiConnectionConfig language_;
  std::string key_;
  std::string body_;
  // The request under way asks for no thinking.
  bool thinking_off_ = false;
  base::TimeTicks started_;
  std::unique_ptr<RemoteModelClient> client_;
  base::WeakPtrFactory<CommandCheck> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_COMMAND_CHECK_H_
