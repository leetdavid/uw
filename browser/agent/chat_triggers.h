// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_CHAT_TRIGGERS_H_
#define UW_BROWSER_AGENT_CHAT_TRIGGERS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "uw/components/agent/trigger.h"

namespace uw {

class TriggerProcess;

// A profile's triggers, one per chat at most: each a command running out of
// sight that wakes its chat. See docs/product.md, "Scheduled chats". The
// delegate, ScheduledChats, delivers each wake: to the chat's agent, or as a
// run.
class ChatTriggers {
 public:
  // Why a trigger stopped before its command ended.
  enum class Stop { kByUser, kByAgent, kAfterADay, kPrintedTooMuch, kPaused };

  struct Trigger {
    std::string command;
    TriggerWake wake = TriggerWake::kEnd;
    // What to do with each wake.
    std::string request;
    base::FilePath folder;
    // Saved on a schedule card: no limits, and each wake starts a run.
    bool lasting = false;
    // What a run on a wake of a chat's trigger uses: the runs model and the
    // chat's approval mode.
    std::string provider_id;
    std::string model;
    std::string effort;
    std::string approval_mode = "auto";
    base::Time started;
    int wakes = 0;
  };

  class Delegate {
   public:
    // Delivers a wake of `chat_id`'s trigger: to the chat's agent, or with
    // `as_run`, as a run. False when it cannot yet, as while a run of the
    // chat is under way; the lines wait for Retry().
    virtual bool OnWake(const std::string& chat_id,
                        const Trigger& trigger,
                        const TriggerWakeLines& wake,
                        bool as_run) = 0;
    // `trigger` stopped before its command ended.
    virtual void OnTriggerStopped(const std::string& chat_id,
                                  const Trigger& trigger,
                                  Stop why) = 0;
    // A trigger started, woke, or went away.
    virtual void OnTriggersChanged(const std::string& chat_id) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit ChatTriggers(Delegate* delegate);
  ChatTriggers(const ChatTriggers&) = delete;
  ChatTriggers& operator=(const ChatTriggers&) = delete;
  ~ChatTriggers();

  // Starts `trigger` for `chat_id`, or says why not.
  base::expected<void, std::string> Start(const std::string& chat_id,
                                          Trigger trigger);
  // Stops the trigger of `chat_id` and its command, and tells the delegate
  // why.
  void StopTrigger(const std::string& chat_id, Stop why);
  // Ends the trigger of `chat_id` without a word, as when the schedule that
  // kept it goes.
  void End(const std::string& chat_id);
  // Ends every trigger without a word, as the profile goes away.
  void EndAll() { running_.clear(); }
  // Makes the trigger of `chat_id` a lasting one, which its schedule keeps.
  void MakeLasting(const std::string& chat_id);
  // Delivers the lines that waited, once the run that kept them ends.
  void Retry(const std::string& chat_id);

  const Trigger* Find(std::string_view chat_id) const;
  // The chats with a running trigger.
  std::vector<std::string> ChatIds() const;
  bool empty() const { return running_.empty(); }

  // Shorter limits, so that tests need not wait hours.
  static void SetLimitsForTesting(const TriggerLimits& limits);

 private:
  struct Running {
    Running();
    ~Running();

    Trigger trigger;
    std::unique_ptr<TriggerProcess> process;
    base::TimeTicks started;
    std::optional<base::TimeTicks> last_wake;
    // The lines since the last wake, and when the first of them came.
    TriggerWakeLines pending;
    base::TimeTicks pending_since;
    // Lines in the minute that started at `minute_start`.
    base::TimeTicks minute_start;
    int minute_lines = 0;
    bool ended = false;
    // A wake that the delegate could not take yet.
    bool waiting = false;
    base::OneShotTimer wake_timer;
    base::OneShotTimer life_timer;
    // Tells apart the callbacks of a trigger that a later one replaced.
    int generation = 0;
  };

  Running* FindRunning(const std::string& chat_id, int generation);
  void OnLine(const std::string& chat_id, int generation, std::string line);
  void OnEnd(const std::string& chat_id, int generation, int code);
  // Wakes the chat once the lines have settled and the limits allow.
  void ScheduleWake(const std::string& chat_id, bool settle);
  void Wake(const std::string& chat_id);

  const raw_ptr<Delegate> delegate_;
  std::map<std::string, std::unique_ptr<Running>, std::less<>> running_;
  int next_generation_ = 1;
  base::WeakPtrFactory<ChatTriggers> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_TRIGGERS_H_
