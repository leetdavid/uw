// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_SCHEDULED_CHATS_H_
#define UW_BROWSER_AGENT_SCHEDULED_CHATS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/important_file_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/device/public/mojom/time_zone_monitor.mojom.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_run.h"
#include "uw/browser/agent/chat_triggers.h"
#include "uw/components/agent/chat_scheduler.h"
#include "uw/components/agent/scheduled_chat.h"
#include "uw/components/ai_providers/providers.h"

class Profile;
class ScopedProfileKeepAlive;

namespace uw {

// A profile's scheduled chats, per docs/product.md, "Scheduled chats": their
// schedules, kept in uw_chats/schedules.json, and their runs, which happen
// out of sight while uw is open, with or without a window. Each run adds a
// line to its chat and tells the user what the chat's settings say to.
class ScheduledChats : public base::SupportsUserData::Data,
                       public ChatIndex::Observer,
                       public ProfileObserver,
                       public ChatTriggers::Delegate,
                       public device::mojom::TimeZoneMonitorClient {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A chat's schedule, or how its run stands, changed.
    virtual void OnScheduleChanged(const std::string& chat_id) = 0;
  };

  // A chat's open page, which takes the lines its runs add, so that the
  // page's own saves keep them.
  class Page {
   public:
    virtual void AddRunLines(base::ListValue lines) = 0;
    // The chat's trigger woke it: `line` goes into the chat, and `message`
    // to its agent, as a steer does. False when the chat cannot take it now,
    // and the wake starts a run instead.
    virtual bool Wake(base::DictValue line, const std::string& message) = 0;
    // A run on a trigger's wake found something worth telling the agent.
    virtual void Tell(const std::string& message) = 0;

   protected:
    virtual ~Page() = default;
  };

  // The scheduled chats of `profile`, made and started on first use. None in
  // a private profile.
  static ScheduledChats* For(Profile* profile);

  explicit ScheduledChats(Profile* profile);
  ScheduledChats(const ScheduledChats&) = delete;
  ScheduledChats& operator=(const ScheduledChats&) = delete;
  ~ScheduledChats() override;

  bool loaded() const { return loaded_; }

  // The schedule of `chat_id`, or null when it has none.
  const ScheduledChat* Find(std::string_view chat_id) const;
  // Whether a run of `chat_id` is under way, and whether it waits for the
  // user to approve a step.
  bool Running(std::string_view chat_id) const;
  bool Waiting(std::string_view chat_id) const;
  // The step a run of `chat_id` waits on, or null.
  const AgentTools::Approval* WaitingStep(std::string_view chat_id) const;
  // When `chat_id` runs next, or nothing while it is paused or done.
  std::optional<base::Time> NextRunOf(std::string_view chat_id) const;
  // The chat that watches `page`, matched by WatchedAddress(), or null.
  const std::string* ChatWatching(const GURL& page) const;
  // The model the last saved schedule card picked, which a new card starts
  // on.
  std::optional<ModelChoice> LastModel() const { return last_model_; }

  // The trigger of `chat_id`, while one runs.
  const ChatTriggers::Trigger* TriggerOf(std::string_view chat_id) const {
    return triggers_.Find(chat_id);
  }
  // Leaves a command running as the trigger of `chat_id`, or says why not.
  base::expected<void, std::string> StartTrigger(const std::string& chat_id,
                                                 ChatTriggers::Trigger trigger);
  // Stops the trigger of `chat_id`, as its user or its agent asks. A trigger
  // the schedule card keeps is removed on the card. False when none stops.
  bool StopTrigger(const std::string& chat_id, bool by_agent);

  // Gives the lines of `chat_id`'s runs to `page` while it is open, or
  // back to the chat file with null.
  void SetPage(const std::string& chat_id, Page* page);

  // Saves the schedule card's settings for `chat_id` and runs it at once. A
  // card without a page keeps the chat's watched page.
  void Save(const std::string& chat_id, ScheduledChat chat);
  // Makes a chat that watches `chat.page`, which starts with `chat.request`
  // as the user's message, saves `chat` as its schedule, and returns its id.
  std::string Watch(ScheduledChat chat);
  // Removes the schedule. The chat stays.
  void Remove(const std::string& chat_id);
  // Pausing stops runs; resuming does not catch up on the paused time.
  void SetPaused(const std::string& chat_id, bool paused);
  void SetMuted(const std::string& chat_id, bool muted);
  // Pauses, or resumes, every schedule.
  void SetAllPaused(bool paused);
  // Answers the step a run of `chat_id` waits on.
  void AnswerApproval(const std::string& chat_id,
                      AgentLoop::ApprovalAnswer answer);
  // Ends the run of `chat_id` under way.
  void Stop(const std::string& chat_id);
  // Brings the tab a waiting run of `chat_id` waits in next to `chat_tab`,
  // in the user's window.
  void BringWaitingTab(const std::string& chat_id,
                       tabs::TabInterface& chat_tab);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // ChatIndex::Observer:
  void OnChatsChanged() override;

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // device::mojom::TimeZoneMonitorClient:
  void OnTimeZoneChange(const std::string& zone_id) override;

  // ChatTriggers::Delegate:
  bool OnWake(const std::string& chat_id,
              const ChatTriggers::Trigger& trigger,
              const TriggerWakeLines& wake,
              bool as_run) override;
  void OnTriggerStopped(const std::string& chat_id,
                        const ChatTriggers::Trigger& trigger,
                        ChatTriggers::Stop why) override;
  void OnTriggersChanged(const std::string& chat_id) override;

 private:
  struct Run {
    Run();
    Run(Run&&);
    Run& operator=(Run&&);
    ~Run();

    std::unique_ptr<ChatRun> run;
    // The step the run waits on, answered with this.
    std::optional<AgentTools::Approval> step;
    base::OnceCallback<void(AgentLoop::ApprovalAnswer)> approval;
    // Times it was due while the run waited for the user.
    int skipped = 0;
    // For a run on a trigger's wake: the trigger's command, and what woke
    // it.
    std::string trigger;
    std::string wake;
  };

  void OnLoaded(std::optional<base::DictValue> saved);
  // Puts `chat_id` on the scheduler, or takes it off while it is paused.
  void Arm(const std::string& chat_id);
  void UpdateKeepAlive();
  void Persist();
  void Changed(const std::string& chat_id);

  void OnDue(const std::string& chat_id, base::Time when);
  // Starts a run of the schedule. On a wake of its lasting trigger, `wake`
  // tells what woke it.
  void StartRun(const std::string& chat_id,
                base::Time when,
                const ChatTriggers::Trigger* trigger = nullptr,
                std::string wake = std::string());
  // The model `choice` names in `format`, with its key, or nothing when it is
  // not set up.
  std::optional<AgentModel> ModelFor(const ModelChoice& choice);
  // A run on a wake of a chat's own trigger, which no schedule keeps.
  void StartTriggerRun(const std::string& chat_id,
                       ChatTriggers::Trigger trigger,
                       TriggerWakeLines wake);
  void OnTriggerRunFormat(const std::string& chat_id,
                          ChatTriggers::Trigger trigger,
                          TriggerWakeLines wake,
                          base::Time when,
                          std::optional<ProviderFormat> format);
  void OnTriggerRunDone(const std::string& chat_id,
                        std::string command,
                        base::Time when,
                        ChatRun::Outcome outcome);
  // Starts the trigger the schedule of `chat_id` keeps, unless it is paused
  // or runs.
  void StartLastingTrigger(const std::string& chat_id);
  // Adds `lines` to the chat: to its open page, or to its file.
  void AddLines(const std::string& chat_id, base::ListValue lines);
  void OnFormat(const std::string& chat_id,
                base::Time when,
                std::optional<ProviderFormat> format);
  void OnApproval(const std::string& chat_id,
                  AgentTools::Approval approval,
                  base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer);
  void OnRunDone(const std::string& chat_id,
                 base::Time when,
                 ChatRun::Outcome outcome);
  void Notify(const std::string& chat_id,
              const std::string& what,
              const std::string& text);

  const raw_ptr<Profile> profile_;
  base::ImportantFileWriter writer_;
  bool loaded_ = false;
  // Set once the profile starts to go away: nothing runs after.
  bool ending_ = false;
  std::map<std::string, ScheduledChat, std::less<>> chats_;
  std::map<std::string, Run, std::less<>> runs_;
  std::map<std::string, raw_ptr<Page>, std::less<>> pages_;
  std::optional<ModelChoice> last_model_;
  ChatTriggers triggers_{this};
  ChatScheduler scheduler_;
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive_;
  mojo::Receiver<device::mojom::TimeZoneMonitorClient> time_zone_receiver_{
      this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_observation_{
      this};
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<ScheduledChats> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_SCHEDULED_CHATS_H_
