// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "url/gurl.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/agent/chat_files.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"
#include "uw/components/agent/skills.h"
#include "uw/components/ai_providers/providers.h"

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

namespace uw {

// Runs the agent of one chat, tells the chat's page each step, and saves the
// chat after every change. Also keeps the page's list of every saved chat.
// Owned by the page's controller, so the agent stops when the tab closes.
// Until approvals land, nothing here asks before a click or a keystroke.
class UwChatHandler : public mojom::ChatHandler,
                      public ChatIndex::Observer,
                      public Connectors::Observer,
                      public ScheduledChats::Observer,
                      public ScheduledChats::Page,
                      public content::WebContentsObserver {
 public:
  // What the uw command sees of a chat it started or picked up again. While
  // one watches, uw sends none of the chat's notifications. See
  // docs/plans/uw-command.md#chats.
  class Watcher : public base::CheckedObserver {
   public:
    virtual void OnStep(const mojom::Step& step) = 0;
    // A step waits for approval, or, with null, nothing waits any more.
    virtual void OnApproval(const mojom::Approval* approval) = 0;
    virtual void OnUsage(const mojom::ChatUsage& usage) = 0;
    // The chat's model, effort, or approval mode changed.
    virtual void OnSettings() = 0;
    // The chat's tab closed.
    virtual void OnGone() = 0;
  };

  // What the uw command asks a chat.
  struct CommandAsk {
    CommandAsk();
    CommandAsk(CommandAsk&&);
    CommandAsk& operator=(CommandAsk&&);
    ~CommandAsk();

    std::string text;
    // Text piped in with it, which the agent takes as information.
    std::string material;
    // The files attached with --file.
    std::vector<base::FilePath> files;
    // Picked for this chat alone; the mode new chats start on stays.
    std::optional<ApprovalMode> mode;
    // A model of the chat's provider, and one of its effort levels.
    std::string model;
    std::string effort;
    // Keeps a new chat out of agent memory, as --no-memory does.
    bool memory_off = false;
  };

  // The handler of the chat `id` an open tab shows, or null.
  static UwChatHandler* ForChat(std::string_view id);
  // The chats open tabs show.
  static std::vector<UwChatHandler*> Open();

  UwChatHandler(content::WebContents* contents,
                mojo::PendingRemote<mojom::ChatPage> page,
                mojo::PendingReceiver<mojom::ChatHandler> receiver);
  UwChatHandler(const UwChatHandler&) = delete;
  UwChatHandler& operator=(const UwChatHandler&) = delete;
  ~UwChatHandler() override;

  // mojom::ChatHandler:
  void Send(const std::string& text,
            std::vector<mojom::AttachedFilePtr> files) override;
  void Resume() override;
  void TryAgain() override;
  void OpenChat(const std::string& id) override;
  void NewChat() override;
  void ShowScheduled() override;
  void DeleteChat(const std::string& id) override;
  void ShowTab(int32_t number) override;
  void SetSideBySide(bool on, int32_t number) override;
  void GetSuggestions(GetSuggestionsCallback callback) override;
  void AnswerSkill(bool save) override;
  void AnswerApproval(mojom::ApprovalChoice choice,
                      const std::string& note) override;
  void SetModel(const std::string& provider_id,
                const std::string& model) override;
  void SetEffort(const std::string& effort) override;
  void SetMode(mojom::ApprovalMode mode) override;
  void Stop() override;
  void GetConnectors(GetConnectorsCallback callback) override;
  void SetConnectorOn(const std::string& name, bool on) override;
  void SetMemoryOff(bool off) override;
  void SendPrompt(const std::string& connector,
                  const std::string& prompt,
                  const base::flat_map<std::string, std::string>& values,
                  const std::string& shown) override;
  void SetSeesImages(bool sees) override;
  void SetReadsPdfs(bool reads) override;
  void NewSchedule(NewScheduleCallback callback) override;
  void SaveSchedule(mojom::ChatSchedulePtr schedule) override;
  void RemoveSchedule() override;
  void ReopenScheduleCard(const std::string& args) override;
  void DescribePage(const std::string& page,
                    DescribePageCallback callback) override;
  void SetSchedulePaused(bool paused) override;
  void SetScheduleMuted(bool muted) override;
  void CheckNow() override;
  void StopRun() override;
  void StopTrigger() override;

  const std::string& id() const { return id_; }
  bool loaded() const { return loaded_; }
  bool working() const { return Working(); }
  // The step that waits for approval, or null.
  const mojom::Approval* waiting() const { return waiting_.get(); }
  // The chat's last line that is not a model switch, as the chat file keeps
  // it, such as {"kind": "answer", "text": ...}.
  const base::DictValue* last_line() const { return LastLine(); }
  content::WebContents* contents() { return contents_; }
  const std::optional<ModelChoice>& model() const { return model_; }
  ApprovalMode mode() const { return mode_; }
  // The skills the user may start by typing / and a name.
  std::vector<Skill> UserSkills() const;
  void AddWatcher(Watcher* watcher);
  void RemoveWatcher(Watcher* watcher);
  // Sends `ask` once its mode and model are set, or steers the agent at work
  // with it.
  void AskFromCommand(CommandAsk ask);
  // Sends a connector's prompt with `values` as its arguments, as picking it
  // in the / menu does.
  void PromptFromCommand(
      const std::string& connector,
      const std::string& prompt,
      const base::flat_map<std::string, std::string>& values);

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

  // ScheduledChats::Page:
  void AddRunLines(base::ListValue lines) override;
  bool Wake(base::DictValue line, const std::string& message) override;
  void Tell(const std::string& message) override;

  // Connectors::Observer:
  void OnConnectorsChanged() override;

  void Steer(const std::string& text,
             std::vector<mojom::AttachedFilePtr> files) override;
  void SaveFile(const std::string& name,
                const std::vector<uint8_t>& chunk,
                const std::string& saved,
                SaveFileCallback callback) override;
  void ReadFile(const std::string& saved, ReadFileCallback callback) override;
  void OpenFile(const std::string& saved) override;
  void ShowUserTab(const std::string& id) override;
  void ShowUserTask(const std::string& id) override;

  // ChatIndex::Observer:
  void OnChatsChanged() override;

  // content::WebContentsObserver:
  void OnVisibilityChanged(content::Visibility visibility) override;

 private:
  void OnLoaded(std::optional<base::DictValue> saved);
  // Tells the page about the chat's schedule and how it stands, and shows a
  // step its run waits on.
  void SendSchedule();
  // Tells the page what the context holds and what the chat has cost.
  void SendUsage();
  // What memory does in this chat, as the page and the agent take it.
  mojom::ChatMemory MemoryNow() const;
  void SendMemory();
  // While the chat shows and its run waits for approval, brings the tab the
  // run waits in next to the chat.
  void BringWaitingTab();
  // The schedule card's state for `chat`, which runs on `schedule`.
  mojom::ScheduleStatePtr StateOf(const ScheduledChat& chat) const;
  // What a new schedule card starts with. See NewSchedule.
  ScheduledChat NewCard() const;
  // The agent's schedule call: opens the schedule card filled in, over the
  // chat's schedule if it has one, and tells the model that nothing is saved
  // until the user saves it.
  base::DictValue OpenScheduleCard(const base::DictValue& args);
  // The agent's trigger and stop_trigger calls.
  base::DictValue StartTrigger(const TriggerAsk& ask,
                               const base::FilePath& folder);
  base::DictValue StopTriggerForAgent();
  // Gives the agent what the chat's scheduled runs reported, from their
  // lines.
  void UpdateScheduledRuns();
  // Starts the agent on the chat's model, giving it back the tabs it had.
  // False, after telling the page why, when the chat has no model it can
  // reach or the page is not in a tab.
  bool Start();
  // The chat's model as the agent reaches it. Nothing when the chat has no
  // model, or its provider is gone.
  std::optional<AgentModel> AgentModelNow() const;
  // Runs the agent on the chat's model from now on, marks the switch once
  // the chat has begun, and tells the page.
  void OnModelChanged();
  void OnModelFormat(const std::string& provider_id,
                     const std::string& model,
                     std::optional<ProviderFormat> format);
  // The last line of the log that is not a model switch.
  const base::DictValue* LastLine() const;
  void OnStep(const AgentLoop::Step& step);
  // Tells the page and the watchers about `step`.
  void ShowStep(mojom::StepPtr step);
  void SendFromCommand(const std::string& text,
                       std::string material,
                       std::vector<base::FilePath> files);
  // The command's files are in the chat's folder: the page prepares them.
  void OnCommandFilesCopied(std::string text,
                            std::string material,
                            std::vector<mojom::SentFilePtr> copied);
  void OnCommandFilesPrepared(std::string text,
                              std::string material,
                              std::vector<mojom::AttachedFilePtr> files);
  // Where the chat keeps copies of its attached files.
  base::FilePath FilesFolder() const;
  // The page's attached files, as the agent takes them.
  std::vector<Attachment> AttachmentsFrom(
      const std::vector<mojom::AttachedFilePtr>& files) const;
  void SendAttached(const std::string& text, std::vector<Attachment> files);
  void SteerAttached(const std::string& text, std::vector<Attachment> files);
  // Steers with `text` once what the model gets of its files is read.
  void SteerRead(std::string text, ReadFiles read);
  bool Working() const;
  // One line of the saved log as the page shows it.
  mojom::StepPtr LineFor(const base::DictValue& entry) const;
  void Log(base::DictValue entry);
  void Save();
  // The agent's context changed: saves the chat and tells the page.
  void OnContextChanged();
  // Tells the chat list how this chat stands.
  void UpdateIndex();
  // Sends the chat's notification saying `what` happened, with `text`,
  // unless the user is looking: the chat shows in the window they use.
  void Notify(const std::string& what, const std::string& text);

  // The agent's open tab `number`, or null.
  tabs::TabInterface* AgentTab(int number);
  // Brings the card of the tab a tool step acted in up to date, and takes a
  // new picture of that tab.
  void OnToolStep(const AgentLoop::Step& step);
  void OnPicture(int number, const content::CopyFromSurfaceResult& result);
  void OnScreen(int number, const std::string& text, int cursor_line);
  // Tells the page about the agent's tabs.
  void SendTabs();

  // Reads the skills in uw's folder and ~/.agents/skills, then runs `done`.
  void LoadSkills(base::OnceClosure done);
  void OnSkillsLoaded(base::OnceClosure done, std::vector<Skill> skills);
  std::vector<Skill> EnabledSkills() const;
  // Tells the watchers that the model, effort, or approval mode changed.
  void TellSettings();
  // The skill `text` starts with as "/name", if the user may start one by
  // that name, and what they typed after it.
  std::optional<std::pair<Skill, std::string>> UserSkill(
      std::string_view text) const;
  std::vector<ConnectorTools> ChatConnectors() const;
  // Gives the agent the chat's connectors, as they are now.
  void UpdateConnectors();
  void OnPrompt(std::string shown,
                std::string name,
                base::expected<mcp::Content, std::string> answer);
  // Sends `text`, which started `skill`, once its SKILL.md has been read.
  void SendStartedSkill(std::string text,
                        std::vector<Attachment> files,
                        Skill skill,
                        std::string arguments,
                        std::optional<std::string> file);
  // Sends a message the chat shows as `shown` and the model gets as `sent`,
  // once what the model gets of its files is read. `started` is "skill" or
  // "prompt" when the message started one, called `name`.
  void SendToAgent(const std::string& shown,
                   const std::string& sent,
                   std::vector<Attachment> files,
                   std::string_view started = {},
                   std::string_view name = {});
  void SendRead(std::string shown,
                std::string sent,
                std::string started,
                std::string name,
                ReadFiles read);
  // The agent's save_skill call: shows the page the skill, and answers once
  // the user does.
  void AskToSaveSkill(base::DictValue args,
                      base::OnceCallback<void(base::DictValue)> answer);
  void OnSkillWritten(bool written);
  // A step the user must approve: shows the page the approval card and
  // answers once the user does.
  void AskApproval(AgentTools::Approval approval,
                   base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer);
  // A program in the agent's terminal tab `number` waits for a password,
  // which only the user types; 0 once nothing waits.
  void OnPassword(int number, const std::string& program);

  const raw_ptr<content::WebContents> contents_;
  mojo::Remote<mojom::ChatPage> page_;
  mojo::Receiver<mojom::ChatHandler> receiver_;
  std::string id_;
  // None in a private window, where nothing is kept.
  std::unique_ptr<ChatFile> file_;
  bool loaded_ = false;
  std::string title_;
  // The model the chat runs on. Nothing when no model is picked anywhere.
  std::optional<ModelChoice> model_;
  ApprovalMode mode_ = ApprovalMode::kAuto;
  // The user kept this chat out of agent memory.
  bool memory_off_ = false;
  PrefChangeRegistrar prefs_;
  // What the page shows, in order. Steps are kept in the context.
  base::ListValue log_;
  // The saved conversation and the agent's tabs by number, by the tree's page
  // ids, until the agent starts again.
  std::unique_ptr<AgentContext> saved_context_;
  std::map<int, std::string> saved_tabs_;
  std::unique_ptr<AgentLoop> agent_;
  // When the user last wrote to the chat, for the chat list's order.
  base::Time updated_;
  // An answer came while the page was out of sight.
  bool unread_ = false;
  // What the cards on the page show of each of the agent's tabs, kept after
  // a tab closes.
  struct Card {
    std::string title;
    std::string url;
    std::string favicon;
    std::string picture;
    // A terminal tab shows the last lines of its screen in place of a
    // picture.
    bool terminal = false;
    std::string screen;
  };
  std::map<int, Card> cards_;
  // The tab the agent acted in last.
  int current_tab_ = 0;
  std::vector<Skill> skills_;
  bool skills_loaded_ = false;
  // A started skill's SKILL.md or a connector's prompt is being read.
  bool preparing_ = false;
  // The connectors this chat turned off, kept here before its agent starts.
  std::set<std::string> connectors_off_;
  bool sees_images_ = true;
  bool reads_pdfs_ = false;
  // Writes and reads the chat's attached files in order.
  scoped_refptr<base::SequencedTaskRunner> files_sequence_;
  base::ScopedObservation<Connectors, Connectors::Observer>
      connectors_observation_{this};
  struct Message {
    std::string text;
    std::vector<Attachment> files;
  };
  // What the user sent before the skills were read, to send once they are.
  std::optional<Message> waiting_message_;
  bool waiting_resume_ = false;
  // The message that could not be sent because the chat had no agent, for
  // Try again.
  std::optional<Message> unsent_;
  // What the command bar or the new tab page opened this chat with, until it
  // is asked.
  std::optional<ChatStart> start_;
  // The start picked a model, so the chat is asked once the model is set.
  bool ask_after_model_ = false;
  // Tells the agent about the tab handed to it, before the first message.
  std::string handed_over_;
  // The skill the agent asked to save, and its answer.
  base::DictValue proposed_skill_;
  base::OnceCallback<void(base::DictValue)> skill_answer_;
  // The answer to the step that waits for the user's approval, and the step.
  base::OnceCallback<void(AgentLoop::ApprovalAnswer)> approval_answer_;
  mojom::ApprovalPtr waiting_;
  // The uw command started this chat.
  bool from_terminal_ = false;
  // Text piped into the command with the message on its way to the agent.
  std::string material_;
  base::ObserverList<Watcher> watchers_;
  // The agent's terminal tab that waits for the user's password, or 0.
  int password_tab_ = 0;
  // Lines of the chat's runs that came before the chat was read, and whether
  // the step a run waits on shows.
  base::ListValue early_run_lines_;
  bool run_step_shown_ = false;
  // DescribePage's lookups in history and favicons.
  base::CancelableTaskTracker page_tasks_;
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer>
      schedules_observation_{this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_observation_{
      this};
  base::WeakPtrFactory<UwChatHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
