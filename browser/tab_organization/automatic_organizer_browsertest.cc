// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
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
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/model_test_server.h"
#include "uw/browser/tab_organization/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"

namespace uw {
namespace {

// Serves pages to browse and plays every model provider.
class AutomaticOrganizerBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  static std::unique_ptr<net::test_server::HttpResponse> ServePage(
      const net::test_server::HttpRequest& request) {
    const std::string path(request.GetURL().path());
    if (!path.starts_with("/page/")) {
      return nullptr;
    }
    const std::string name = path.substr(6);
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/html");
    response->set_content("<html><head><title>" + name +
                          "</title></head><body>Notes about " + name +
                          ".</body></html>");
    return response;
  }

  void Answer(const std::string& path, std::string body) {
    models_.Answer(path, std::move(body));
  }

  std::vector<ModelRequest> Requests() { return models_.Requests(); }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void UseDecisionModel(DecisionProvider provider) {
    ASSERT_TRUE(ApiKeyStore().SetKey(kDecisionKeyAccount, "decision-key"));
    prefs()->SetInteger(kDecisionProviderPref, static_cast<int>(provider));
    prefs()->SetString(kDecisionEndpointPref,
                       embedded_test_server()->GetURL("/v1").spec());
    prefs()->SetString(kDecisionModelPref, "decision-model");
    prefs()->SetString(kDecisionKeyAccountPref, kDecisionKeyAccount);
    prefs()->SetBoolean(kOrganizationEnabledPref, true);
  }

  void UseLanguageModel() {
    ASSERT_TRUE(ApiKeyStore().SetKey(kLanguageKeyAccount, "language-key"));
    prefs()->SetInteger(kOrganizationModePref,
                        static_cast<int>(ConnectionMode::kOpenAiCompatible));
    prefs()->SetString(kOrganizationEndpointPref,
                       embedded_test_server()->GetURL("/v1").spec());
    prefs()->SetString(kOrganizationModelPref, "language-model");
    prefs()->SetString(kOrganizationKeyAccountPref, kLanguageKeyAccount);
    prefs()->SetBoolean(kOrganizationEnabledPref, true);
  }

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  AutomaticOrganizer* organizer() {
    return AutomaticOrganizer::From(browser());
  }

  // The Organize bar at the foot of the sidebar.
  OrganizeBar* bar() {
    return FindBar(BrowserView::GetBrowserViewForBrowser(browser()));
  }

  static OrganizeBar* FindBar(views::View* view) {
    if (OrganizeBar* bar = views::AsViewClass<OrganizeBar>(view)) {
      return bar;
    }
    for (views::View* child : view->children()) {
      if (OrganizeBar* bar = FindBar(child)) {
        return bar;
      }
    }
    return nullptr;
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
  [[nodiscard]] testing::AssertionResult SettleAfter(size_t request_count) {
    organizer()->DispatchNowForTesting();
    if (base::test::RunUntil([&] {
          return Requests().size() >= request_count &&
                 organizer()->pending_requests() == 0;
        })) {
      return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "the test server received " << Requests().size() << " of "
           << request_count << " requests and "
           << organizer()->pending_requests() << " are still waiting";
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
  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
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
  ASSERT_TRUE(bar()->notice_visible());
  EXPECT_EQ(bar()->notice_text(), u"Moved to Plan Lisbon trip");
  SaveWindowScreenshot(browser(), "automatic-move");

  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/decisions");
  EXPECT_EQ(requests[0].authorization, "Bearer decision-key");
  EXPECT_NE(requests[0].body.find("Title: lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Notes about lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Plan Lisbon trip"), std::string::npos);

  bar()->UndoNotice();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(controller()->DepthFor(link), 1);
  EXPECT_FALSE(bar()->notice_visible());
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
  ASSERT_TRUE(bar()->notice_visible());
  EXPECT_EQ(bar()->notice_text(), u"Created task Plan Lisbon trip");

  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[0].path, "/v1/systemone");
  EXPECT_EQ(requests[0].authorization, "Bearer decision-key");
  EXPECT_NE(requests[0].body.find("\"criteria\""), std::string::npos);
  EXPECT_EQ(requests[1].path, "/v1/chat/completions");
  EXPECT_EQ(requests[1].authorization, "Bearer language-key");

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
  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/chat/completions");
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       UnsureAndFailedAnswersLeaveTheTabWhereItIs) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->activity_text(), u"The request for lisbon failed.");

  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.89));
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(link->GetGroup());
  ASSERT_TRUE(bar()->activity_visible());
  EXPECT_EQ(bar()->activity_text(),
            u"The model suggested Plan Lisbon trip for hotels. Confidence: "
            u"0.89. uw moves a tab at 0.90 or more.");
  SaveWindowScreenshot(browser(), "activity");

  Answer("/v1/decisions", DecisionsAnswer("scratchpad", 0.99));
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->activity_text(),
            u"The model left weather where it is. Confidence: 0.99.");

  Answer("/v1/decisions", "not json");
  Browse(link, "museums");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_EQ(bar()->activity_text(),
            u"The model's answer for museums could not be read.");
  EXPECT_FALSE(bar()->notice_visible());
  EXPECT_FALSE(controller()->CanUndo() && link->GetGroup());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheSidebarSaysWhenAModelIsAskedAndThenClears) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  std::vector<AutomaticOrganizer::Event::Kind> kinds;
  const base::CallbackListSubscription events = organizer()->ObserveEvents(
      base::BindLambdaForTesting([&](const AutomaticOrganizer::Event& event) {
        kinds.push_back(event.kind);
      }));

  OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));

  EXPECT_EQ(kinds, (std::vector{AutomaticOrganizer::Event::Kind::kAsking,
                                AutomaticOrganizer::Event::Kind::kUnsure}));
  EXPECT_EQ(bar()->activity_text(),
            u"Asking decision-model about lisbon\u2026");
  EXPECT_FALSE(bar()->activity_visible());
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
                       AManualMoveHidesTheMessage) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_TRUE(bar()->notice_visible());

  ASSERT_TRUE(controller()->MoveToScratchpad({Id(link)}));
  EXPECT_FALSE(bar()->notice_visible());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OrganizeMenuUndoesPausesAndOpensSettings) {
  OneTask();
  ASSERT_TRUE(bar()->GetVisible());
  ASSERT_TRUE(bar()->IsCommandIdEnabled(OrganizeBar::kUndo));
  bar()->ExecuteCommand(OrganizeBar::kUndo, 0);
  EXPECT_TRUE(tabs()->group_model()->ListTabGroups().empty());
  EXPECT_FALSE(bar()->IsCommandIdEnabled(OrganizeBar::kUndo));

  EXPECT_FALSE(bar()->IsCommandIdEnabled(OrganizeBar::kResume));
  bar()->ExecuteCommand(OrganizeBar::kPauseIndefinitely, 0);
  EXPECT_EQ(ReadPauseState(prefs()), PauseMode::kIndefinite);
  EXPECT_FALSE(bar()->IsCommandIdEnabled(OrganizeBar::kPauseIndefinitely));
  bar()->ExecuteCommand(OrganizeBar::kResume, 0);
  EXPECT_EQ(ReadPauseState(prefs()), PauseMode::kOff);

  EXPECT_FALSE(bar()->IsCommandIdChecked(OrganizeBar::kShowActivity));
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  EXPECT_TRUE(prefs()->GetBoolean(kOrganizationShowActivityPref));
  EXPECT_TRUE(bar()->IsCommandIdChecked(OrganizeBar::kShowActivity));

  bar()->ExecuteCommand(OrganizeBar::kSettings, 0);
  EXPECT_EQ(tabs()->GetActiveWebContents()->GetVisibleURL(),
            GURL("chrome://settings/uwOrganization"));
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OrganizeMenuOpensATabInANewTask) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(1);
  ASSERT_EQ(in_task->GetGroup(), task);
  tabs()->ActivateTabAt(1);
  const int before = tabs()->count();

  bar()->ExecuteCommand(OrganizeBar::kNewTask, 0);

  ASSERT_EQ(tabs()->count(), before + 1);
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  ASSERT_TRUE(opened->GetGroup());
  EXPECT_NE(opened->GetGroup(), task);
  EXPECT_EQ(TitleOf(*opened->GetGroup()), u"New task");
  EXPECT_EQ(controller()->DepthFor(opened), 0);
  EXPECT_EQ(in_task->GetGroup(), task);
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
