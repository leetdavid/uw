// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AUTOMATIC_ORGANIZER_H_
#define UW_BROWSER_TAB_ORGANIZATION_AUTOMATIC_ORGANIZER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/components/tab_organization/ai_protocol.h"
#include "uw/components/tab_organization/page_tree.h"

class BrowserWindowInterface;
class Profile;
class TabStripModel;

namespace content_extraction {
struct InnerTextResult;
}

namespace uw {

class TabOrganizationController;

// Files tab branches into tasks in the background. When a page's title or
// URL changes, it asks the decision model, or the language model when there
// is none, where the branch belongs, and applies a confident answer through
// the controller as one undoable action. It never blocks browsing: a slow,
// failed, or stale answer changes nothing.
class AutomaticOrganizer : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(AutomaticOrganizer);
  static constexpr double kMinConfidence = 0.90;

  // One step of the organizer's work on a tab, for the sidebar.
  struct Event {
    enum class Kind {
      // A request went out. `detail` is the model that was asked.
      kAsking,
      kNaming,
      // The tab moved into the task named `detail`, or started it.
      kMoved,
      kCreated,
      // The model chose to leave the tab where it is.
      kLeft,
      // The model suggested `detail` with less than kMinConfidence.
      kUnsure,
      kRequestFailed,
      kAnswerUnreadable,
      // The tab changed or was moved before the answer arrived.
      kDropped,
    };
    Kind kind;
    // The tab's title.
    std::u16string page;
    std::u16string detail;
    double confidence = 0.0;
  };

  AutomaticOrganizer(BrowserWindowInterface* window,
                     TabOrganizationController* controller);
  ~AutomaticOrganizer() override;

  static AutomaticOrganizer* From(BrowserWindowInterface* window);

  using EventCallback = base::RepeatingCallback<void(const Event&)>;
  base::CallbackListSubscription ObserveEvents(EventCallback callback);
  size_t pending_requests() const { return requests_.size(); }
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
    BranchContext context;
    std::unique_ptr<RemoteModelClient> client;
  };

  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;

  Profile* profile() const;
  bool Enabled() const;
  void OnConfigChanged();
  // Notes title and URL changes and starts the debounce timer for them.
  void Scan();
  void OnManualMove(const std::vector<PageId>& pages);
  void Dispatch();
  void OnExcerpt(PageId page,
                 uint64_t revision,
                 std::unique_ptr<content_extraction::InnerTextResult> result);
  void SendDecision(const PageId& page, uint64_t revision, std::string excerpt);
  void OnDecision(uint64_t request_id, RemoteModelClient::Outcome outcome);
  void OnName(uint64_t request_id, RemoteModelClient::Outcome outcome);
  void CreateTask(const Request& request, std::u16string title);
  void Report(Event::Kind kind,
              const Request& request,
              std::u16string detail = std::u16string(),
              double confidence = 0.0);
  // True while the branch is where it was, and unchanged, since the request.
  bool Current(const Request& request) const;
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
