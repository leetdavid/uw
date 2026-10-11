// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/apple/scoped_fake_keychain_v2.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

// The fake keychain answers every lookup with this key.
constexpr char kStoredKey[] = "Bearer mock_password";

struct ProviderRequest {
  std::string path;
  std::string body;
  std::string authorization;
};

std::string DecisionsAnswer(const std::string& choice, double confidence) {
  return R"({"answers":[{"type":"choice","name":"destination","choice":")" +
         choice + R"(","confidence":)" + base::NumberToString(confidence) +
         "}]}";
}

std::string SystemOneAnswer(const std::string& choice, double confidence) {
  return R"({"answers":{"destination":{"type":"choice","choice":")" + choice +
         R"(","confidence":)" + base::NumberToString(confidence) +
         R"(,"probabilities":{}}}})";
}

std::string ChatAnswer(const std::string& escaped_content) {
  return R"({"choices":[{"message":{"role":"assistant","content":")" +
         escaped_content + R"("}}]})";
}

// Serves pages to browse and plays every model provider.
class AutomaticOrganizerBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AutomaticOrganizerBrowserTest::Serve, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    const std::string path(request.GetURL().path());
    if (path.starts_with("/page/")) {
      const std::string name = path.substr(6);
      response->set_content_type("text/html");
      response->set_content("<html><head><title>" + name +
                            "</title></head><body>Notes about " + name +
                            ".</body></html>");
      return response;
    }
    if (!path.starts_with("/v1/")) {
      return nullptr;
    }
    base::AutoLock hold(lock_);
    const auto authorization = request.headers.find("Authorization");
    requests_.push_back({path, request.content,
                         authorization == request.headers.end()
                             ? std::string()
                             : authorization->second});
    const auto answer = answers_.find(path);
    if (answer == answers_.end()) {
      response->set_code(net::HTTP_NOT_FOUND);
      return response;
    }
    response->set_content_type("application/json");
    response->set_content(answer->second);
    return response;
  }

  void Answer(const std::string& path, std::string body) {
    base::AutoLock hold(lock_);
    answers_[path] = std::move(body);
  }

  std::vector<ProviderRequest> Requests() {
    base::AutoLock hold(lock_);
    return requests_;
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void UseDecisionModel(DecisionProvider provider) {
    ASSERT_TRUE(ApiKeyStore().SetKey("decision", "decision-key"));
    prefs()->SetInteger(kDecisionProviderPref, static_cast<int>(provider));
    prefs()->SetString(kDecisionEndpointPref,
                       embedded_test_server()->GetURL("/v1").spec());
    prefs()->SetString(kDecisionModelPref, "decision-model");
    prefs()->SetString(kDecisionKeyAccountPref, "decision");
    prefs()->SetBoolean(kOrganizationEnabledPref, true);
  }

  void UseLanguageModel() {
    ASSERT_TRUE(ApiKeyStore().SetKey("organization", "language-key"));
    prefs()->SetInteger(kOrganizationModePref,
                        static_cast<int>(ConnectionMode::kOpenAiCompatible));
    prefs()->SetString(kOrganizationEndpointPref,
                       embedded_test_server()->GetURL("/v1").spec());
    prefs()->SetString(kOrganizationModelPref, "language-model");
    prefs()->SetString(kOrganizationKeyAccountPref, "organization");
    prefs()->SetBoolean(kOrganizationEnabledPref, true);
  }

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  AutomaticOrganizer* organizer() {
    return AutomaticOrganizer::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  tabs::TabInterface* InitialTab() { return tabs()->GetTabAtIndex(0); }

  tabs::TabInterface* NewTab() {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    tabs::TabInterface* tab = tabs()->GetActiveTab();
    base::RunLoop().RunUntilIdle();
    return tab;
  }

  // Cmd-clicks a link in `source` that leads to the served page `name`.
  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source,
                                   const std::string& name) {
    NavigateParams params(browser(),
                          embedded_test_server()->GetURL("/page/" + name),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    content::WebContents* contents = params.navigated_or_inserted_contents;
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(contents);
  }

  void Browse(tabs::TabInterface* tab, const std::string& name) {
    content::WebContents* contents = tab->GetContents();
    contents->GetController().LoadURL(
        embedded_test_server()->GetURL("/page/" + name), content::Referrer(),
        ui::PAGE_TRANSITION_TYPED, std::string());
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    base::RunLoop().RunUntilIdle();
  }

  // Returns once any page-text read the organizer started for `tab` is back.
  void WaitForPageText(tabs::TabInterface* tab) {
    base::RunLoop loop;
    content_extraction::GetInnerText(
        *tab->GetContents()->GetPrimaryMainFrame(), std::nullopt,
        base::BindOnce(
            [](base::OnceClosure done,
               std::unique_ptr<content_extraction::InnerTextResult>) {
              std::move(done).Run();
            },
            loop.QuitClosure()));
    loop.Run();
    base::RunLoop().RunUntilIdle();
  }

  // Sends what is due and waits until every answer has been handled.
  [[nodiscard]] bool SettleAfter(size_t request_count) {
    organizer()->DispatchNowForTesting();
    return base::test::RunUntil([&] {
      return Requests().size() >= request_count &&
             organizer()->pending_requests() == 0;
    });
  }

  PageId Id(tabs::TabInterface* tab) {
    return controller()->IdFor(tab).value_or(PageId());
  }

  std::u16string TitleOf(const tab_groups::TabGroupId& group) {
    return tabs()->group_model()->GetTabGroup(group)->visual_data()->title();
  }

  // A window with one ungrouped tab and one task. Returns {ungrouped, task}.
  std::pair<tabs::TabInterface*, tab_groups::TabGroupId> OneTask() {
    tabs::TabInterface* grouped = InitialTab();
    tabs::TabInterface* ungrouped = NewTab();
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask({Id(grouped)}, u"Plan Lisbon trip");
    CHECK(task);
    return {ungrouped, *task};
  }

 private:
  crypto::apple::ScopedFakeKeychainV2 keychain_{"uw-test"};
  base::Lock lock_;
  std::vector<ProviderRequest> requests_ GUARDED_BY(lock_);
  std::map<std::string, std::string> answers_ GUARDED_BY(lock_);
};

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       DecisionModelFilesALinkedTabIntoItsTask) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));

  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_EQ(controller()->DepthFor(link), 1);
  ASSERT_TRUE(SettleAfter(1));

  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(link), 0);
  const std::optional<AutomaticOrganizer::Notice> notice =
      organizer()->TakeNotice();
  ASSERT_TRUE(notice);
  EXPECT_EQ(notice->task, u"Plan Lisbon trip");
  EXPECT_FALSE(notice->created);

  const std::vector<ProviderRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/decisions");
  EXPECT_EQ(requests[0].authorization, kStoredKey);
  EXPECT_NE(requests[0].body.find("Title: lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Notes about lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Plan Lisbon trip"), std::string::npos);

  controller()->Undo();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(controller()->DepthFor(link), 1);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       JevDecidesAndTheLanguageModelNamesTheNewTask) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("new_task", 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Plan Lisbon trip");
  EXPECT_FALSE(InitialTab()->GetGroup());
  const std::optional<AutomaticOrganizer::Notice> notice =
      organizer()->TakeNotice();
  ASSERT_TRUE(notice);
  EXPECT_TRUE(notice->created);

  const std::vector<ProviderRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[0].path, "/v1/systemone");
  EXPECT_EQ(requests[0].authorization, kStoredKey);
  EXPECT_NE(requests[0].body.find("\"criteria\""), std::string::npos);
  EXPECT_EQ(requests[1].path, "/v1/chat/completions");
  EXPECT_EQ(requests[1].authorization, kStoredKey);

  controller()->Undo();
  EXPECT_TRUE(tabs()->group_model()->ListTabGroups().empty());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       DecisionModelAloneCreatesAnUnnamedTask) {
  UseDecisionModel(DecisionProvider::kOpenAi);
  Answer("/v1/decisions", DecisionsAnswer("new_task", 0.93));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(1));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"New task");
  EXPECT_EQ(Requests().size(), 1u);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       WithoutADecisionModelTheLanguageModelDecidesAndNames) {
  UseLanguageModel();
  Answer("/v1/chat/completions",
         ChatAnswer(R"({\"decision\":\"new_task\",\"confidence\":0.95,)"
                    R"(\"name\":\"Read news\"})"));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "news");
  ASSERT_TRUE(SettleAfter(1));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Read news");
  const std::vector<ProviderRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/chat/completions");
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       UnsureAndFailedAnswersLeaveTheTabWhereItIs) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_FALSE(link->GetGroup());

  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.89));
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(link->GetGroup());

  Answer("/v1/decisions", DecisionsAnswer("scratchpad", 0.99));
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_FALSE(link->GetGroup());
  EXPECT_FALSE(organizer()->TakeNotice());
  EXPECT_FALSE(controller()->CanUndo() && link->GetGroup());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AManualMoveBeforeTheAnswerWins) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.99));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");

  organizer()->DispatchNowForTesting();
  const std::optional<tab_groups::TabGroupId> mine =
      controller()->MoveToNewTask({Id(link)}, u"Mine");
  ASSERT_TRUE(mine);
  WaitForPageText(link);

  EXPECT_EQ(link->GetGroup(), mine);
  EXPECT_TRUE(Requests().empty());
  EXPECT_EQ(organizer()->pending_requests(), 0u);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       PauseStopsRequestsUntilResumed) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));
  SetPauseState(prefs(), PauseMode::kIndefinite);

  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_TRUE(Requests().empty());
  EXPECT_FALSE(link->GetGroup());

  SetPauseState(prefs(), PauseMode::kOff);
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_EQ(link->GetGroup(), task);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OrganizationOffSendsNothing) {
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  prefs()->SetBoolean(kOrganizationEnabledPref, false);
  Answer("/v1/decisions", DecisionsAnswer("new_task", 0.99));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_TRUE(Requests().empty());
  EXPECT_FALSE(link->GetGroup());
}

}  // namespace
}  // namespace uw
