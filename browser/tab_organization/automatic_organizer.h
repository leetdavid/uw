// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AUTOMATIC_ORGANIZER_H_
#define UW_BROWSER_TAB_ORGANIZATION_AUTOMATIC_ORGANIZER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/browser/tab_organization/organizer_log.h"
#include "uw/components/tab_organization/ai_protocol.h"
#include "uw/components/tab_organization/page_tree.h"
#include "uw/components/tab_organization/placement.h"

class BrowserWindowInterface;
class Profile;
class TabStripModel;

namespace content_extraction {
struct InnerTextResult;
}

namespace uw {

struct AiConnectionConfig;

class TabOrganizationController;

// Files tab branches into tasks in the background. When a page's title
// changes, or the page goes to another site, it asks the decision model, or
// the language model when there is none, which task the tab belongs to, if
// any. It keeps the answer and decides about the whole branch the tab is in,
// once no request about a tab of that branch is out. Tabs that sit under
// each other and agree move together: into a task, out of a task they no
// longer fit, or into a new task, through the controller as one undoable
// action. A tab that still fits its task stays behind. A new task starts
// under the name every new task has, so its tabs move at once, and the
// language model is asked for its name afterwards. The same answers say when
// another task needs a name: a task without one that the branch starts, or a
// task uw named that has outgrown its name. A name that could not be had is
// asked for again when a tab in that task next changes. It never blocks
// browsing: a slow, failed, or stale answer changes nothing.
class AutomaticOrganizer : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(AutomaticOrganizer);

  // One step of the organizer's work on a tab, for the sidebar.
  struct Event {
    enum class Kind {
      // A request went out.
      kAsking,
      // The language model was asked for a task name. `detail` is that model.
      kNaming,
      // The tab moved into the task named `detail`, or started it. A tab
      // that left its task moved to `detail`, the Scratchpad.
      kMoved,
      kCreated,
      // The tab is already where the answer puts it.
      kLeft,
      // The tab stays, and the language model is asked to name its task.
      kNeedsName,
      // The answer fell short of what a move needs. `detail` is where it
      // would have put the tab.
      kUnsure,
      // `detail` is the HTTP status or the network error.
      kRequestFailed,
      kAnswerUnreadable,
      // The tab changed or was moved before the answer arrived.
      kDropped,
      // No name came of the name the user asked for with Name Task. `detail`
      // is the task's name, and `page` why, when there is a reason to give.
      kNameFailed,
    };
    Kind kind;
    // The tab's title.
    std::u16string page;
    std::u16string detail;
    // The number that decided the answer, and what it had to reach for uw
    // to act. Zero when the model gave no usable answer.
    double confidence = 0.0;
    double needed = 0.0;
    // The model that was asked where the tab belongs.
    std::u16string model;
    // How long its answer took. Zero while the request is out.
    base::TimeDelta took;
    // The task, for kNameFailed.
    std::string task;
  };

  AutomaticOrganizer(BrowserWindowInterface* window,
                     TabOrganizationController* controller);
  ~AutomaticOrganizer() override;

  static AutomaticOrganizer* From(BrowserWindowInterface* window);

  using EventCallback = base::RepeatingCallback<void(const Event&)>;
  base::CallbackListSubscription ObserveEvents(EventCallback callback);
  // Pages a placement request is being prepared or is out for, plus naming
  // requests on their way.
  size_t pending_requests() const;
  // True while the language model is asked for a name for `task`.
  bool IsNaming(const std::string& task) const;
  // Runs when such a request starts or ends.
  base::CallbackListSubscription ObserveNaming(base::RepeatingClosure callback);
  // True when the user can ask for a name: there is a language model, and
  // the window is not a private one. Automatic organization need not be on.
  bool CanAskForName() const;
  // Name Task: asks the language model to name `task` now, from the tabs it
  // holds. The user asked, so the name replaces one they typed, and it is
  // uw's from then on. When no name comes of it, a kNameFailed event says so.
  void AskForName(const std::string& task);
  // Forgets the name asked for `task`, so its answer changes nothing. The
  // user is typing a name.
  void DropAskedName(const std::string& task);
  base::WeakPtr<AutomaticOrganizer> AsWeakPtr();
  // Carries out the last suggestion: an answer that fell short of what a
  // move needs, as one undoable action. Does nothing when the tab has changed
  // or moved since. The acceptance goes to the organizer log as an example
  // the user confirmed.
  void AcceptSuggestion();
  bool HasSuggestion() const { return suggestion_.has_value(); }
  // Sends requests for changed pages now instead of after the debounce delay.
  void DispatchNowForTesting();

 private:
  struct Activity {
    std::u16string title;
    GURL url;
    // Where the shell of a terminal tab is. Empty for a page.
    base::FilePath folder;
    uint64_t revision = 0;
    // The revision already sent, or skipped because the user placed the page.
    uint64_t requested = 0;
    // True from the moment a placement request about the page is prepared
    // until that request ends.
    bool asking = false;
    // True for a page that stays where it is until it shows something else:
    // the user placed it, or uw has acted on its answer.
    bool placed = false;
  };
  struct Request {
    PageId page;
    uint64_t revision = 0;
    std::string task;
    // What `task` was called as the request went out.
    std::u16string task_title;
    PageId parent;
    DecisionFormat format = DecisionFormat::kChat;
    // True when the request listed at least one task.
    bool has_tasks = false;
    // True when the request carried the name check.
    bool checks_name = false;
    // True when `task` had no name as the request went out, so the model was
    // not told about it. The answer is read that way even if the task has a
    // name by the time it arrives.
    bool in_unnamed_task = false;
    BranchContext context;
    // The placement request, and the naming request when there was one.
    OrganizerExchange decision;
    OrganizerExchange naming;
    // What uw made of the answer. See Placement.
    std::string target;
    double confidence = 0.0;
    double needed = 0.0;
    // What the answer gave the name check. Zero when there was none.
    double new_name = 0.0;
    // True once the placement request is in the organizer log.
    bool logged = false;
    // True once the request is for a name for `task`, which the branch stays
    // in.
    bool names_task = false;
    // The tabs under `page` that stay behind when its branch moves.
    std::vector<PageId> staying;
    // How many moves there had been once this one started a task. A later
    // move is what Undo reverses then.
    uint64_t moves = 0;
  };
  // A request on its way, with the client that sends it.
  struct Pending {
    Request request;
    std::unique_ptr<RemoteModelClient> client;
  };
  // A readable answer about what a page shows now, kept until uw acts on it.
  struct Kept {
    Request request;
    ClassificationAnswer answer;
  };

  // The last answer that was only suggested, with its request.
  struct Suggestion {
    Request request;
    std::optional<std::u16string> task_name;
  };
  // A name the user asked for, on its way.
  struct AskedName {
    std::unique_ptr<RemoteModelClient> client;
    OrganizerExchange exchange;
  };

  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;

  Profile* profile() const;
  bool Enabled() const;
  void OnConfigChanged();
  // Notes which pages show something else and starts the debounce timer for
  // them.
  void Scan();
  void OnManualMove(const std::vector<PageId>& pages);
  void Dispatch();
  void OnExcerpt(PageId page,
                 uint64_t revision,
                 std::unique_ptr<content_extraction::InnerTextResult> result);
  void Ask(PageId page, uint64_t revision, std::string excerpt);
  // False when no request went out.
  bool SendDecision(const PageId& page, uint64_t revision, std::string excerpt);
  // The titles of the other tabs that make up the task `page` is in. In a
  // task with a name, a tab that is still being asked about, or that the
  // model put outside the task, is not one of them.
  std::vector<std::string> MemberTitles(const Page& page) const;
  // The titles of the tabs under `top`.
  std::vector<std::string> TitlesUnder(const PageId& top) const;
  // The name check for a tab in a task that uw named and that holds other
  // tabs, when there is a language model to give a new name.
  std::optional<NameCheck> NameCheckFor(const Page& page) const;
  // Names the task the branch stays in: with `name` when the answer carried
  // one, else by asking the language model. With neither, the branch is
  // simply left where it is.
  void NameTaskOf(Request request, std::optional<std::u16string> name);
  // Fills in the naming request for `request` and returns the client that
  // sends it. Null when there is no language model to ask.
  std::unique_ptr<RemoteModelClient> PrepareNaming(
      Request& request,
      const std::vector<std::string>& other_titles);
  using AnswerHandler =
      void (AutomaticOrganizer::*)(uint64_t, RemoteModelClient::Outcome);
  // Sends `request` through `client` and keeps both until `on_answer` takes
  // them.
  void Send(Request request,
            std::unique_ptr<RemoteModelClient> client,
            AnswerHandler on_answer);
  void OnDecision(uint64_t request_id, RemoteModelClient::Outcome outcome);
  void OnName(uint64_t request_id, RemoteModelClient::Outcome outcome);
  void NameFailed(const std::string& task, std::u16string reason);
  void OnAskedName(const std::string& task, RemoteModelClient::Outcome outcome);
  // Decides about the branch `page` is in from the answers kept about its
  // tabs, and carries that out. It waits while a request about one of those
  // tabs is out, and first asks about the tabs nobody asked about when
  // something is about to move.
  void Settle(const PageId& page);
  // Settles every branch that holds a kept answer. Run from a posted task, so
  // that nothing moves while the tab strip reports a change.
  void SettleKept();
  void ScheduleSettle();
  // What the kept answers say about the branch under `top`. Drops the
  // answers that were given about another task or another task name.
  std::vector<BranchPlacement> PlaceBranchOf(const PageId& top);
  // Sends requests about the tabs of `branch` nobody asked about. False when
  // there are none.
  bool AskRest(const std::vector<PageId>& branch);
  void CarryOut(const BranchPlacement& group);
  // Moves the branch of `request.page`, without `request.staying`, to
  // `request.target`: a task, the Scratchpad, or a new task. A new task is
  // named `task_name`, or else starts without a name and gets one from the
  // language model afterwards.
  void Apply(Request request, std::optional<std::u16string> task_name);
  // Marks `pages` as placed by what uw did with `by`, and writes the kept
  // requests about them to the organizer log.
  void Placed(const std::vector<PageId>& pages,
              Event::Kind kind,
              const Request& by);
  // Tells the sidebar. The first report after the answer also writes the
  // placement request to the organizer log.
  void Report(Event::Kind kind,
              Request& request,
              std::u16string detail = std::u16string());
  // True while the branch is where it was, and unchanged, since the request.
  bool Current(const Request& request) const;
  // True while the page shows what the answer was about, in the same task
  // under the same name.
  bool Valid(const Kept& kept) const;
  // What the sidebar calls `request.target`.
  std::u16string TargetName(const Request& request) const;
  // Which page it was, where its branch was, where the answer put it, and
  // the numbers behind it.
  static base::DictValue LogDetails(const Request& request);
  // False for a task that still has the name every new task starts with. The
  // model is not offered such a task: its name says nothing about it.
  bool Named(const std::string& task) const;
  // True while `task` has a name that uw gave it. A name the user typed is
  // theirs, and uw does not replace it.
  bool Generated(const std::string& task) const;
  std::optional<Pending> Take(uint64_t request_id);
  // Works out which tasks a name is being asked for, and tells the observers
  // when that changed.
  void UpdateNaming();
  void CancelRequestsFor(const PageId& page);
  // A request that carries the key in `key_account`, or none when that key
  // is gone. A language model gets it in its own format.
  std::unique_ptr<RemoteModelClient> Client(const GURL& url,
                                            const std::string& key_account,
                                            std::string body);
  std::unique_ptr<RemoteModelClient> Client(const AiConnectionConfig& language,
                                            const std::string& body);

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabOrganizationController> controller_;
  const raw_ptr<TabStripModel> tabs_;
  std::map<PageId, Activity> pages_;
  std::map<uint64_t, Pending> requests_;
  std::map<PageId, Kept> kept_;
  // Top pages of the branches whose unasked tabs are being asked about.
  std::set<PageId> asked_;
  // The names the user asked for, by task.
  std::map<std::string, AskedName> asked_names_;
  // Tasks the language model is being asked to name.
  std::set<std::string> naming_;
  base::RepeatingClosureList naming_changed_;
  // Moves so far, by uw and by the user.
  uint64_t moves_ = 0;
  bool settle_pending_ = false;
  std::optional<Suggestion> suggestion_;
  // Tasks left without a name because the call for one failed or gave nothing
  // usable.
  std::set<std::string> owed_names_;
  uint64_t next_request_ = 1;
  base::RepeatingCallbackList<void(const Event&)> events_;
  base::OneShotTimer timer_;
  PrefChangeRegistrar prefs_;
  base::CallbackListSubscription tree_changed_;
  base::CallbackListSubscription manual_moves_;
  ui::ScopedUnownedUserData<AutomaticOrganizer> user_data_;
  base::WeakPtrFactory<AutomaticOrganizer> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_AUTOMATIC_ORGANIZER_H_
