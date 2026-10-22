// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

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
  // The model the last saved schedule card picked, which a new card starts
  // on.
  std::optional<ModelChoice> LastModel() const { return last_model_; }

  // Gives the lines of `chat_id`'s runs to `page` while it is open, or
  // back to the chat file with null.
  void SetPage(const std::string& chat_id, Page* page);

  // Saves the schedule card's settings for `chat_id` and runs it at once.
  void Save(const std::string& chat_id, ScheduledChat chat);
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

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // ChatIndex::Observer:
  void OnChatsChanged() override;

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // device::mojom::TimeZoneMonitorClient:
  void OnTimeZoneChange(const std::string& zone_id) override;

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
  };

  void OnLoaded(std::optional<base::DictValue> saved);
  // Puts `chat_id` on the scheduler, or takes it off while it is paused.
  void Arm(const std::string& chat_id);
  void UpdateKeepAlive();
  void Persist();
  void Changed(const std::string& chat_id);

  void OnDue(const std::string& chat_id, base::Time when);
  void StartRun(const std::string& chat_id, base::Time when);
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
