// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_QUICK_CHECK_H_
#define UW_BROWSER_AGENT_QUICK_CHECK_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/ai_providers/usage.h"
#include "uw/components/tab_organization/ai_protocol.h"

class Profile;

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

class ModelCatalog;

// One quick check under way, such as the command check or the memory check:
// to the quick-choice model, a decision model or a language model, or else
// to the chat's own model. A language model checks with thinking off
// where the catalog says how, and is asked again without that if its
// provider rejects it; one with no known way gets 90 seconds. Destroying the
// check drops it. See docs/plans/agent-chats.md and
// docs/plans/agent-memory.md.
class QuickCheck {
 public:
  // The check's request to `model` in `format`, kChat for a language model,
  // or nothing when it has none for it.
  using BuildBody = base::RepeatingCallback<std::optional<std::string>(
      DecisionFormat format,
      std::string_view model)>;
  // The model's answer and the format it was asked in.
  struct Answer {
    DecisionFormat format = DecisionFormat::kChat;
    std::string body;
  };
  using Done = base::OnceCallback<void(std::optional<Answer>)>;

  // Sends the check that `build` makes, with `chat_model` and `chat_key` for
  // the chat's own model. `done` gets the answer, or nothing for no answer.
  // Null when no model can take it.
  static std::unique_ptr<QuickCheck> Send(Profile& profile,
                                          const AiConnectionConfig& chat_model,
                                          const std::string& chat_key,
                                          BuildBody build,
                                          Done done);

  QuickCheck(const QuickCheck&) = delete;
  QuickCheck& operator=(const QuickCheck&) = delete;
  ~QuickCheck();

  // What the check's answer used, the model that gave it, and that model's
  // price. Nothing until it answers, or when its provider says nothing.
  const std::optional<ModelUsage>& usage() const { return usage_; }
  const std::string& model() const { return model_name_; }
  const std::optional<ModelPrice>& price() const { return price_; }

 private:
  QuickCheck(Profile& profile, Done done);

  void SendLanguageCheck(const ModelCatalog* catalog);
  void SendToLanguageModel(const AiConnectionConfig& model,
                           const std::string& body,
                           base::TimeDelta timeout);
  void Start(std::unique_ptr<RemoteModelClient> client);
  void OnAnswer(RemoteModelClient::Outcome outcome);

  scoped_refptr<network::SharedURLLoaderFactory> loader_;
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
  std::optional<ModelUsage> usage_;
  std::string model_name_;
  std::optional<ModelPrice> price_;
  std::unique_ptr<RemoteModelClient> client_;
  base::WeakPtrFactory<QuickCheck> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_QUICK_CHECK_H_
