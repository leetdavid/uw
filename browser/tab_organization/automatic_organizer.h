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
#include "uw/browser/tab_organization/organizer_log.h"
#include "uw/browser/tab_organization/remote_model_client.h"
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

class TabOrganizationController;

// Files tab branches into tasks in the background. When a page's title
// changes, or the page goes to another site, it asks the decision model, or
// the language model when there is none, which task the branch belongs to,
// if any. A confident answer moves
// the branch into that task, out of a task it no longer fits, or into a new
// task, through the controller as one undoable action. The same answer says
// when a task needs a name from the language model: a task without one that
// the branch starts, or a task uw named that has outgrown its name. A name
// that could not be had is asked for again when a tab in that task next
// changes. It never blocks browsing: a slow, failed, or stale answer changes
// nothing.
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
  };

  AutomaticOrganizer(BrowserWindowInterface* window,
                     TabOrganizationController* controller);
  ~AutomaticOrganizer() override;

  static AutomaticOrganizer* From(BrowserWindowInterface* window);

  using EventCallback = base::RepeatingCallback<void(const Event&)>;
  base::CallbackListSubscription ObserveEvents(EventCallback callback);
  size_t pending_requests() const { return requests_.size(); }
  // Carries out the last suggestion: an answer that fell short of what a
  // move needs, as one undoable action. Does nothing when the tab has changed
  // or moved since. The acceptance goes to the organizer log as an example
  // the user confirmed.
  void AcceptSuggestion();
  // Sends requests for changed pages now instead of after the debounce delay.
  void DispatchNowForTesting();

 private:
  struct Activity {
    std::u16string title;
    GURL url;
    uint64_t revision = 0;
    // The revision already sent, or skipped because the user placed the page.
    uint64_t requested = 0;
  };
  struct Request {
    PageId page;
    uint64_t revision = 0;
    std::string task;
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
    std::unique_ptr<RemoteModelClient> client;
  };

  // The last answer that was only suggested, with its request.
  struct Suggestion {
    Request request;
    std::optional<std::u16string> task_name;
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
  void SendDecision(const PageId& page, uint64_t revision, std::string excerpt);
  // The titles of the other tabs in the task `page` is in.
  std::vector<std::string> OtherTitles(const Page& page) const;
  // The name check for a branch in a task that uw named and that holds other
  // tabs, when there is a language model to give a new name.
  std::optional<NameCheck> NameCheckFor(const Page& page) const;
  // Names the task the branch stays in: with `name` when the answer carried
  // one, else by asking the language model. With neither, the branch is
  // simply left where it is.
  void NameTaskOf(Request request, std::optional<std::u16string> name);
  // Fills in the naming request for `request`. False when there is no
  // language model to ask.
  bool PrepareNaming(Request& request,
                     const std::vector<std::string>& other_titles);
  using AnswerHandler =
      void (AutomaticOrganizer::*)(uint64_t, RemoteModelClient::Outcome);
  // Sends `request.client` and keeps the request until `on_answer` takes it.
  void Send(Request request, AnswerHandler on_answer);
  void OnDecision(uint64_t request_id, RemoteModelClient::Outcome outcome);
  void OnName(uint64_t request_id, RemoteModelClient::Outcome outcome);
  // Moves the branch to `request.target`: a task, the Scratchpad, or a new
  // task, named `task_name` or else by the language model.
  void Apply(Request request, std::optional<std::u16string> task_name);
  // The task, when it was started.
  std::optional<tab_groups::TabGroupId> CreateTask(Request& request,
                                                   std::u16string title);
  // Tells the sidebar. The first report after the answer also writes the
  // placement request to the organizer log.
  void Report(Event::Kind kind,
              Request& request,
              std::u16string detail = std::u16string());
  // True while the branch is where it was, and unchanged, since the request.
  bool Current(const Request& request) const;
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
  std::optional<Request> Take(uint64_t request_id);
  void CancelRequestsFor(const PageId& page);
  std::unique_ptr<RemoteModelClient> Client(const GURL& url,
                                            const std::string& key_account,
                                            std::string body);

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabOrganizationController> controller_;
  const raw_ptr<TabStripModel> tabs_;
  std::map<PageId, Activity> pages_;
  std::map<uint64_t, Request> requests_;
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
