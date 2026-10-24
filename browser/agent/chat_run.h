// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_CHAT_RUN_H_
#define UW_BROWSER_AGENT_CHAT_RUN_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/components/agent/run_report.h"

class Profile;

namespace uw {

// One run of a scheduled chat, per docs/product.md, "Scheduled chats": a
// subagent that starts fresh in the profile's run window, out of sight, with a
// brief from RunBrief(), and ends by calling the report tool. Its tabs close
// when it is destroyed.
class ChatRun {
 public:
  struct Outcome {
    enum class Kind {
      // It reported what it found.
      kReported,
      // It ended without a report. Counts toward the pause after three.
      kFailed,
      // It could not reach the model, as when the provider is down, rejects
      // the key, or the Mac is offline. Does not count.
      kModelUnreachable,
      // The user stopped it.
      kStopped,
    };
    Kind kind = Kind::kFailed;
    RunReport report;
    // What went wrong, for a run that did not report.
    std::string error;
    // Its steps, which fold under its line in the chat.
    std::vector<AgentLoop::Step> steps;
    // What its model requests used and cost.
    UsageTotal usage;
  };
  using Done = base::OnceCallback<void(Outcome)>;

  // Starts the run. `done` runs once, after the run ends.
  ChatRun(Profile* profile,
          AgentModel model,
          ApprovalMode mode,
          std::string_view brief,
          Done done);
  ChatRun(const ChatRun&) = delete;
  ChatRun& operator=(const ChatRun&) = delete;
  ~ChatRun();

  // Asks the user whether a step may happen. The run waits for the answer.
  void SetApprover(AgentLoop::Approve approve);
  void Stop();

  // The agent's tab `number`, or null.
  tabs::TabInterface* TabNumbered(int number);

 private:
  void OnStep(const AgentLoop::Step& step);
  base::DictValue OnReport(const base::DictValue& args);
  void End(Outcome outcome);

  tabs::TabHandle run_tab_;
  std::unique_ptr<AgentLoop> loop_;
  std::optional<RunReport> report_;
  std::vector<AgentLoop::Step> steps_;
  Done done_;
  base::WeakPtrFactory<ChatRun> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_RUN_H_
