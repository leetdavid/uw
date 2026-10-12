// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
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
#include "net/base/url_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/model_test_server.h"
#include "uw/browser/tab_organization/organizer_log.h"
#include "uw/browser/tab_organization/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/components/tab_organization/placement.h"

namespace uw {
namespace {

// Serves pages to browse and plays every model provider.
class AutomaticOrganizerBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    // A second site, served by the same test server.
    host_resolver()->AddRule("other.test", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    // Careful thresholds: a move and a new name need 0.90, a new task 0.75.
    // A test of other values sets its own.
    SaveThresholds(prefs(), {0.90, 0.75, 0.90});
  }

  static std::unique_ptr<net::test_server::HttpResponse> ServePage(
      const net::test_server::HttpRequest& request) {
    const std::string path(request.GetURL().path());
    if (!path.starts_with("/page/")) {
      return nullptr;
    }
    // /page/<name> is titled <name>, unless ?title= says otherwise.
    std::string name = path.substr(6);
    net::GetValueForKeyInQuery(request.GetURL(), "title", &name);
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

  // Opens a blank tab beside `tab`, in its task and under its parent.
  tabs::TabInterface* NewTabBeside(tabs::TabInterface* tab) {
    tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tab));
    return NewTab();
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

  // Every visible view inside `view` lies within its parent and is at least
  // as tall as it wants to be.
  static void ExpectNothingClipped(const views::View* view) {
    for (const views::View* child : view->children()) {
      if (!child->GetVisible()) {
        continue;
      }
      EXPECT_TRUE(view->GetLocalBounds().Contains(child->bounds()))
          << child->GetClassName() << " at " << child->bounds().ToString()
          << " in " << view->GetLocalBounds().ToString();
      EXPECT_GE(child->height(), child->GetPreferredSize().height())
          << child->GetClassName();
      ExpectNothingClipped(child);
    }
  }

 private:
  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
};

// Starts uw with --uw-organizer-log.
class OrganizerLogBrowserTest : public AutomaticOrganizerBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    AutomaticOrganizerBrowserTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    command_line->AppendSwitchPath(kOrganizerLogSwitch, LogPath());
  }

  base::FilePath LogPath() const {
    return folder_.GetPath().AppendASCII("organizer-log.jsonl");
  }

  std::string LogText() {
    base::ThreadPoolInstance::Get()->FlushForTesting();
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string text;
    base::ReadFileToString(LogPath(), &text);
    return text;
  }

  // What uw did with each request of `purpose`, in order.
  std::vector<std::string> Results(const std::string& purpose) {
    std::vector<std::string> results;
    for (const base::DictValue& line : LogLines()) {
      if (*line.FindString("purpose") == purpose) {
        results.push_back(*line.FindString("result"));
      }
    }
    return results;
  }

  // The lines written so far, each one a JSON object.
  std::vector<base::DictValue> LogLines() {
    std::vector<base::DictValue> lines;
    for (const std::string& line :
         base::SplitString(LogText(), "\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)) {
      std::optional<base::DictValue> parsed =
          base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
      EXPECT_TRUE(parsed) << line;
      if (parsed) {
        lines.push_back(std::move(*parsed));
      }
    }
    return lines;
  }

 private:
  base::ScopedTempDir folder_;
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
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.95 of 0.90");
  EXPECT_TRUE(
      bar()->card()->source().starts_with(u"Answered by decision-model in "));
  EXPECT_TRUE(bar()->card()->offers_undo());
  EXPECT_FALSE(bar()->card()->waiting());
  SaveWindowScreenshot(browser(), "automatic-move");

  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/decisions");
  EXPECT_EQ(requests[0].authorization, "Bearer decision-key");
  EXPECT_NE(requests[0].body.find("Title: lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Notes about lisbon"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Plan Lisbon trip"), std::string::npos);
  EXPECT_NE(requests[0].body.find("Source: opened from"), std::string::npos);

  bar()->Undo();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(controller()->DepthFor(link), 1);
  EXPECT_FALSE(bar()->card_visible());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       JevDecidesAndTheLanguageModelNamesTheNewTask) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Plan Lisbon trip");
  EXPECT_FALSE(InitialTab()->GetGroup());
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Created task Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.97 of 0.75");
  EXPECT_TRUE(bar()->card()->offers_undo());

  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[0].path, "/v1/systemone");
  EXPECT_EQ(requests[0].authorization, "Bearer decision-key");
  EXPECT_NE(requests[0].body.find("\"criteria\""), std::string::npos);
  // With no task to belong to, only the second question is asked.
  EXPECT_EQ(requests[0].body.find("\"destination\""), std::string::npos);
  EXPECT_NE(requests[0].body.find("\"otherwise\""), std::string::npos);
  EXPECT_EQ(requests[1].path, "/v1/chat/completions");
  EXPECT_EQ(requests[1].authorization, "Bearer language-key");

  controller()->Undo();
  EXPECT_TRUE(tabs()->group_model()->ListTabGroups().empty());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       DecisionModelAloneCreatesAnUnnamedTask) {
  UseDecisionModel(DecisionProvider::kOpenAi);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.93, 0.8));

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
         ChatAnswer(R"({\"task\":\"none\",\"confidence\":0.95,)"
                    R"(\"new_task\":0.9,\"name\":\"Read news\"})"));

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
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"The request failed: HTTP 404");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
  EXPECT_EQ(bar()->card()->confidence(), u"");
  EXPECT_FALSE(bar()->card()->offers_undo());

  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.89));
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(link->GetGroup());
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Suggested Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->page(), u"hotels");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.89 of 0.90");
  EXPECT_FALSE(bar()->card()->offers_undo());
  SaveWindowScreenshot(browser(), "activity");

  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.1));
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->card()->headline(), u"Left where it is");
  EXPECT_EQ(bar()->card()->page(), u"weather");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.99 of 0.90");

  Answer("/v1/decisions", "not json");
  Browse(link, "museums");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_EQ(bar()->card()->headline(), u"Could not read the answer");
  EXPECT_EQ(bar()->card()->page(), u"museums");
  EXPECT_EQ(bar()->card()->confidence(), u"");
  EXPECT_FALSE(bar()->card()->offers_undo());
  EXPECT_FALSE(controller()->CanUndo() && link->GetGroup());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheSidebarSaysWhenAModelIsAskedAndThenClears) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  // What the card showed as each step was reported.
  std::vector<AutomaticOrganizer::Event::Kind> kinds;
  std::vector<bool> shown;
  std::vector<bool> waiting;
  const base::CallbackListSubscription events = organizer()->ObserveEvents(
      base::BindLambdaForTesting([&](const AutomaticOrganizer::Event& event) {
        kinds.push_back(event.kind);
        shown.push_back(bar()->card_visible());
        waiting.push_back(bar()->card()->waiting());
      }));

  OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));

  EXPECT_EQ(kinds, (std::vector{AutomaticOrganizer::Event::Kind::kAsking,
                                AutomaticOrganizer::Event::Kind::kUnsure}));
  EXPECT_EQ(shown, (std::vector{true, false}));
  EXPECT_EQ(waiting, (std::vector{true, true}));
  EXPECT_EQ(bar()->card()->headline(), u"Asking decision-model");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
  EXPECT_EQ(bar()->card()->source(), u"");
  EXPECT_FALSE(bar()->card_visible());
}

// A sidebar full of tabs leaves the card exactly its own height. Nothing in
// it may be cut off, and it may not sit under the buttons around it.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheCardFitsInACrowdedSidebar) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
      gfx::Rect(0, 0, 1000, 600));
  // Blank tabs load at once, so nothing is still spinning in the screenshot.
  for (int i = 0; i < 30; ++i) {
    chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                     /*foreground=*/false);
  }
  base::RunLoop().RunUntilIdle();
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.39));
  OpenLinkFrom(ungrouped,
               "a-page-whose-title-is-far-too-long-for-the-sidebar-to-show");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_TRUE(bar()->card_visible());
  views::test::RunScheduledLayout(
      BrowserView::GetBrowserViewForBrowser(browser()));

  const views::View* card = bar()->card();
  const gfx::Rect card_bounds = card->GetBoundsInScreen();
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.39 of 0.90");
  EXPECT_TRUE(bar()->GetBoundsInScreen().Contains(card_bounds));
  EXPECT_GE(card->height(), card->GetPreferredSize().height());
  ExpectNothingClipped(card);
  for (const views::View* sibling : bar()->children()) {
    if (sibling != card && sibling->GetVisible()) {
      EXPECT_FALSE(sibling->GetBoundsInScreen().Intersects(card_bounds))
          << sibling->GetClassName();
    }
  }
  for (const views::View* sibling : bar()->parent()->children()) {
    if (sibling != bar() && sibling->GetVisible() &&
        !sibling->GetProperty(views::kViewIgnoredByLayoutKey)) {
      EXPECT_FALSE(
          sibling->GetBoundsInScreen().Intersects(bar()->GetBoundsInScreen()))
          << sibling->GetClassName();
    }
  }
  SaveWindowScreenshot(browser(), "activity-crowded");
}

IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       EveryRequestAndItsAnswerIsWritten) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));

  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 2u);
  const base::DictValue& placement = lines[0];
  EXPECT_EQ(*placement.FindString("purpose"), "placement");
  EXPECT_EQ(*placement.FindString("model"), "decision-model");
  EXPECT_EQ(*placement.FindString("endpoint"),
            embedded_test_server()->GetURL("/v1/systemone").spec());
  EXPECT_EQ(*placement.FindString("result"), "new_task");
  EXPECT_EQ(placement.FindDouble("confidence"), 0.97);
  EXPECT_EQ(placement.FindDouble("needed"), 0.75);
  EXPECT_EQ(*placement.FindString("from"), "");
  EXPECT_EQ(*placement.FindString("to"), "new_task");
  EXPECT_EQ(placement.FindInt("http_status"), 200);
  EXPECT_NE(
      placement.FindStringByDottedPath("request.state")->find("Title: lisbon"),
      std::string::npos);
  EXPECT_EQ(
      *placement.FindStringByDottedPath("answer.answers.otherwise.choice"),
      "new_task");

  const base::DictValue& name = lines[1];
  EXPECT_EQ(*name.FindString("purpose"), "name");
  EXPECT_EQ(*name.FindString("model"), "language-model");
  EXPECT_EQ(*name.FindString("result"), "named");
  EXPECT_NE(name.FindListByDottedPath("request.messages"), nullptr);

  const std::string text = LogText();
  EXPECT_EQ(text.find("decision-key"), std::string::npos);
  EXPECT_EQ(text.find("language-key"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       AConfirmedSuggestionMovesTheTabAndIsLogged) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_FALSE(link->GetGroup());
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Suggested Plan Lisbon trip");
  EXPECT_TRUE(bar()->card()->offers_accept());
  EXPECT_FALSE(bar()->card()->offers_undo());
  SaveWindowScreenshot(browser(), "suggestion");

  bar()->AcceptSuggestion();
  EXPECT_EQ(link->GetGroup(), task);
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Plan Lisbon trip");
  EXPECT_TRUE(bar()->card()->offers_undo());
  EXPECT_FALSE(bar()->card()->offers_accept());
  EXPECT_EQ(Requests().size(), 1u);

  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(*lines[0].FindString("purpose"), "placement");
  EXPECT_EQ(*lines[0].FindString("result"), "unsure");
  const base::DictValue& accepted = lines[1];
  EXPECT_EQ(*accepted.FindString("purpose"), "accepted");
  EXPECT_EQ(*accepted.FindString("result"), task.ToString());
  EXPECT_EQ(*accepted.FindString("to"), task.ToString());
  EXPECT_EQ(accepted.FindDouble("confidence"), 0.5);
  ASSERT_TRUE(accepted.FindDict("request"));
  EXPECT_NE(base::WriteJson(*accepted.FindDict("request"))
                .value_or(std::string())
                .find("Title: lisbon"),
            std::string::npos);
  EXPECT_TRUE(accepted.FindDict("answer"));

  bar()->Undo();
  EXPECT_FALSE(link->GetGroup());
  bar()->AcceptSuggestion();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(LogLines().size(), 2u);
}

// A blank tab opens inside the task of the tab beside it. Once the user takes
// it somewhere that fits no task, it leaves for the Scratchpad.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ATabThatFitsNoTaskLeavesItForTheScratchpad) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(1);
  ASSERT_EQ(in_task->GetGroup(), task);
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));
  tabs::TabInterface* link = OpenLinkFrom(in_task, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_EQ(link->GetGroup(), task);

  tabs::TabInterface* blank = NewTabBeside(link);
  ASSERT_EQ(blank->GetGroup(), task);
  ASSERT_EQ(controller()->DepthFor(blank), 1);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.97, 0.1));
  Browse(blank, "weather");
  ASSERT_TRUE(SettleAfter(2));

  EXPECT_FALSE(blank->GetGroup());
  EXPECT_EQ(link->GetGroup(), task);
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Scratchpad");
  EXPECT_EQ(bar()->card()->page(), u"weather");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.97 of 0.90");
  EXPECT_TRUE(bar()->card()->offers_undo());

  // Only the tab a link opened is described as coming from its parent.
  std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_NE(requests[0].body.find("Source: opened from"), std::string::npos);
  EXPECT_NE(requests[1].body.find("Source: no source tab"), std::string::npos);

  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(*lines[0].FindString("result"), "left");
  EXPECT_EQ(*lines[1].FindString("result"), "moved");
  EXPECT_EQ(*lines[1].FindString("from"), task.ToString());
  EXPECT_EQ(*lines[1].FindString("to"), "scratchpad");
  EXPECT_EQ(lines[1].FindDouble("confidence"), 0.97);

  bar()->Undo();
  EXPECT_EQ(blank->GetGroup(), task);

  // A tab the user reuses for something else is no longer opened from
  // anything either.
  Browse(link, "news");
  ASSERT_TRUE(SettleAfter(3));
  requests = Requests();
  EXPECT_NE(requests[2].body.find("Source: no source tab"), std::string::npos);
  EXPECT_FALSE(link->GetGroup());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATabThatClearlyStartsATaskLeavesForItsOwn) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.8));
  Answer("/v1/chat/completions", ChatAnswer("Compare laptops"));
  tabs::TabInterface* blank = NewTabBeside(tabs()->GetTabAtIndex(1));
  ASSERT_EQ(blank->GetGroup(), task);

  Browse(blank, "laptops");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(blank->GetGroup());
  EXPECT_NE(blank->GetGroup(), task);
  EXPECT_EQ(TitleOf(*blank->GetGroup()), u"Compare laptops");
  EXPECT_EQ(bar()->card()->headline(), u"Created task Compare laptops");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.80 of 0.75");
}

// The tabs under a page came from what it showed before, so the page does
// not take them out of their task without the user.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       APageWithTabsUnderItStaysUntilTheUserConfirms) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* page = tabs()->GetTabAtIndex(1);
  ASSERT_EQ(page->GetGroup(), task);
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));
  tabs::TabInterface* link = OpenLinkFrom(page, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_EQ(link->GetGroup(), task);

  Answer("/v1/decisions", DecisionsAnswer("none", 0.97, 0.1));
  Browse(page, "weather");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(page->GetGroup(), task);
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Suggested Scratchpad");
  ASSERT_TRUE(bar()->card()->offers_accept());

  bar()->AcceptSuggestion();
  EXPECT_FALSE(page->GetGroup());
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Scratchpad");
  EXPECT_TRUE(bar()->card()->offers_undo());
}

// A task that still has the name every new task starts with tells the model
// nothing, so it is not offered, and its tabs are not taken out of it.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATaskWithoutANameIsNotOfferedAndKeepsItsTabs) {
  OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  bar()->ExecuteCommand(OrganizeBar::kNewTask, 0);
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  const std::optional<tab_groups::TabGroupId> unnamed = opened->GetGroup();
  ASSERT_TRUE(unnamed);
  ASSERT_EQ(TitleOf(*unnamed), u"New task");

  Answer("/v1/decisions", DecisionsAnswer("none", 0.97, 0.1));
  Browse(opened, "weather");
  ASSERT_TRUE(SettleAfter(1));

  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_NE(requests[0].body.find("Plan Lisbon trip"), std::string::npos);
  EXPECT_EQ(requests[0].body.find("New task"), std::string::npos);
  EXPECT_EQ(opened->GetGroup(), unnamed);
  EXPECT_EQ(bar()->card()->headline(), u"Left where it is");

  // An answer that names it anyway cannot be used.
  Answer("/v1/decisions", DecisionsAnswer(unnamed->ToString(), 0.99));
  tabs::TabInterface* link = OpenLinkFrom(opened, "forecast");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(bar()->card()->headline(), u"Could not read the answer");
  EXPECT_EQ(link->GetGroup(), unnamed);

  // A tab in it does not start a second task. The one it is in may be that
  // task, once it has a name.
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.99));
  Browse(link, "radar");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_EQ(link->GetGroup(), unnamed);
  EXPECT_EQ(bar()->card()->headline(), u"Left where it is");
}

// A task that has no name gets one when the decision model finds that a tab
// in it starts a piece of work. A one-off names nothing, and an unusable name
// leaves the task for the next tab that does.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ATaskWithoutANameIsNamedWhenATabStartsIt) {
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.1));
  Answer("/v1/chat/completions", ChatAnswer(""));
  bar()->ExecuteCommand(OrganizeBar::kNewTask, 0);
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  const std::optional<tab_groups::TabGroupId> task = opened->GetGroup();
  ASSERT_TRUE(task);

  Browse(opened, "weather");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_EQ(Requests().size(), 1u);
  EXPECT_EQ(TitleOf(*task), u"New task");
  EXPECT_EQ(bar()->card()->headline(), u"Left where it is");

  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.99));
  Browse(opened, "lisbon");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_EQ(opened->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"New task");
  EXPECT_EQ(bar()->card()->headline(), u"Naming the task");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.99 of 0.75");
  EXPECT_FALSE(bar()->card()->offers_undo());

  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  tabs::TabInterface* link = OpenLinkFrom(opened, "hotels");
  ASSERT_TRUE(SettleAfter(5));
  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Plan Lisbon trip");
  std::vector<std::string> naming;
  for (const ModelRequest& request : Requests()) {
    if (request.path == "/v1/chat/completions") {
      naming.push_back(request.body);
    }
  }
  ASSERT_EQ(naming.size(), 2u);
  EXPECT_EQ(naming[0].find("Other tabs in the task"), std::string::npos);
  EXPECT_NE(naming[1].find("Title: hotels"), std::string::npos);
  EXPECT_NE(naming[1].find("Other tabs in the task:\\n- lisbon"),
            std::string::npos);
  EXPECT_THAT(Results("placement"),
              testing::ElementsAre("left", "name", "name"));
  EXPECT_THAT(Results("name"), testing::ElementsAre("no usable name", "named"));

  // A task with a name is one the model can choose.
  Answer("/v1/decisions", DecisionsAnswer(task->ToString(), 0.99));
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(6));
  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 6u);
  EXPECT_EQ(requests[5].path, "/v1/decisions");
  EXPECT_NE(requests[5].body.find("Plan Lisbon trip"), std::string::npos);
  EXPECT_EQ(TitleOf(*task), u"Plan Lisbon trip");
  EXPECT_EQ(link->GetGroup(), task);
}

// A tab that no longer matches the name of its task has either left the task
// or shows that the task has outgrown the name. The decision model says
// which, and only a name uw gave is replaced.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ATaskThatOutgrowsItsNameIsRenamed) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Compare Baixa hotels"));
  tabs::TabInterface* first = OpenLinkFrom(InitialTab(), "baixa");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = first->GetGroup();
  ASSERT_TRUE(task);
  ASSERT_EQ(TitleOf(*task), u"Compare Baixa hotels");
  // A tab outside every task has no name to check.
  EXPECT_EQ(Requests()[0].body.find("needs a new name"), std::string::npos);

  // Alone, this answer would start a second task.
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.9, 0.93));
  Answer("/v1/chat/completions", ChatAnswer("Compare Lisbon hotels"));
  tabs::TabInterface* second = OpenLinkFrom(first, "chiado");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_EQ(second->GetGroup(), task);
  EXPECT_EQ(tabs()->group_model()->ListTabGroups().size(), 1u);
  EXPECT_EQ(TitleOf(*task), u"Compare Lisbon hotels");
  EXPECT_EQ(bar()->card()->headline(), u"Naming the task");
  EXPECT_EQ(bar()->card()->page(), u"chiado");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.93 of 0.90");
  EXPECT_FALSE(bar()->card()->offers_undo());
  std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 4u);
  EXPECT_NE(requests[2].body.find(
                "The user's browsing task \\\"Compare Baixa hotels\\\" holds "
                "this tab and these tabs:\\n- baixa\\nDecide whether the task "
                "needs a new name."),
            std::string::npos);
  EXPECT_EQ(requests[3].path, "/v1/chat/completions");
  EXPECT_NE(requests[3].body.find("Other tabs in the task:\\n- baixa"),
            std::string::npos);

  // Short of what a new name needs, the task keeps its name. It is offered
  // under the new one.
  Answer("/v1/systemone", SystemOneAnswer(task->ToString(), 0.95, 0.0, 0.89));
  Browse(second, "alfama");
  ASSERT_TRUE(SettleAfter(5));
  EXPECT_EQ(TitleOf(*task), u"Compare Lisbon hotels");
  EXPECT_EQ(bar()->card()->headline(), u"Left where it is");
  requests = Requests();
  ASSERT_EQ(requests.size(), 5u);
  EXPECT_NE(requests[4].body.find("\"Compare Lisbon hotels\""),
            std::string::npos);

  // A name the user typed is theirs. The model is not asked about it, and a
  // tab that fits no task leaves as it did before.
  tabs()->ChangeTabGroupVisuals(
      *task,
      tab_groups::TabGroupVisualData(
          u"Lisbon",
          tabs()->group_model()->GetTabGroup(*task)->visual_data()->color()));
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.9, 0.99));
  Answer("/v1/chat/completions", ChatAnswer("Read about Sintra"));
  Browse(second, "sintra");
  ASSERT_TRUE(SettleAfter(7));
  requests = Requests();
  ASSERT_EQ(requests.size(), 7u);
  EXPECT_EQ(requests[5].body.find("needs a new name"), std::string::npos);
  EXPECT_EQ(TitleOf(*task), u"Lisbon");
  ASSERT_TRUE(second->GetGroup());
  EXPECT_NE(second->GetGroup(), task);
  EXPECT_EQ(TitleOf(*second->GetGroup()), u"Read about Sintra");

  EXPECT_THAT(Results("placement"),
              testing::ElementsAre("new_task", "name", "left", "new_task"));
  EXPECT_THAT(Results("name"), testing::ElementsAre("named", "named", "named"));
  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 7u);
  EXPECT_EQ(lines[2].FindDouble("new_name"), 0.93);
  EXPECT_EQ(lines[2].FindDouble("needed"), 0.90);
  EXPECT_EQ(lines[4].FindDouble("new_name"), 0.89);
  EXPECT_EQ(lines[5].FindDouble("new_name"), 0.0);
}

// The names uw gave are saved with the session, so a task can still get a
// new name after a restart, and a name the user typed is still theirs.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       PRE_ANameUwGaveIsRememberedAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  Browse(InitialTab(), "notes");
  const std::optional<tab_groups::TabGroupId> mine =
      controller()->MoveToNewTask({Id(InitialTab())}, u"Mine");
  ASSERT_TRUE(mine);

  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Compare Baixa hotels"));
  tabs::TabInterface* first = OpenLinkFrom(InitialTab(), "baixa");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> named = first->GetGroup();
  ASSERT_TRUE(named);
  ASSERT_NE(named, mine);
  Answer("/v1/systemone", SystemOneAnswer(named->ToString(), 0.95, 0.0, 0.1));
  tabs::TabInterface* second = OpenLinkFrom(first, "chiado");
  ASSERT_TRUE(SettleAfter(3));
  ASSERT_EQ(second->GetGroup(), named);

  EXPECT_TRUE(controller()->HasGeneratedName(named->ToString()));
  EXPECT_FALSE(controller()->HasGeneratedName(mine->ToString()));
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ANameUwGaveIsRememberedAfterARestart) {
  std::optional<tab_groups::TabGroupId> named;
  std::optional<tab_groups::TabGroupId> mine;
  for (const tab_groups::TabGroupId& group :
       tabs()->group_model()->ListTabGroups()) {
    (TitleOf(group) == u"Mine" ? mine : named) = group;
  }
  ASSERT_TRUE(named);
  ASSERT_TRUE(mine);
  ASSERT_EQ(TitleOf(*named), u"Compare Baixa hotels");
  EXPECT_TRUE(controller()->HasGeneratedName(named->ToString()));
  EXPECT_FALSE(controller()->HasGeneratedName(mine->ToString()));

  // So the model is asked about the restored name, and not about the user's.
  tabs::TabInterface* restored = nullptr;
  tabs::TabInterface* own = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
    (tab->GetGroup() == named ? restored : own) = tab;
  }
  ASSERT_TRUE(restored);
  ASSERT_TRUE(own);
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer(named->ToString(), 0.95, 0.0, 0.1));
  OpenLinkFrom(restored, "alfama");
  OpenLinkFrom(own, "minutes");
  const auto request_about = [&](const std::string& title) {
    for (const ModelRequest& request : Requests()) {
      if (request.body.find("Title: " + title) != std::string::npos) {
        return request.body;
      }
    }
    return std::string();
  };
  organizer()->DispatchNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !request_about("alfama").empty() &&
           !request_about("minutes").empty();
  }));
  EXPECT_NE(request_about("alfama").find(
                "The user's browsing task \\\"Compare Baixa hotels\\\" holds"),
            std::string::npos);
  EXPECT_EQ(request_about("minutes").find("needs a new name"),
            std::string::npos);
}

// A tab opened from another tab starts in that tab's task, under it. When it
// turns out to be a piece of work of its own, it leaves for a task of its
// own, and the name check does not hold it back.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AChildTabBreaksOutOfItsParentsTask) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  tabs::TabInterface* parent = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = parent->GetGroup();
  ASSERT_TRUE(task);

  Answer("/v1/systemone", SystemOneAnswer(task->ToString(), 0.95, 0.0, 0.1));
  tabs::TabInterface* sibling = OpenLinkFrom(parent, "hotels");
  ASSERT_TRUE(SettleAfter(3));
  ASSERT_EQ(sibling->GetGroup(), task);
  ASSERT_EQ(controller()->DepthFor(sibling), 1);

  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.8, 0.3));
  Answer("/v1/chat/completions", ChatAnswer("Compare laptops"));
  tabs::TabInterface* child = OpenLinkFrom(parent, "laptops");
  ASSERT_EQ(child->GetGroup(), task);
  ASSERT_EQ(controller()->DepthFor(child), 1);
  ASSERT_TRUE(SettleAfter(5));

  ASSERT_TRUE(child->GetGroup());
  EXPECT_NE(child->GetGroup(), task);
  EXPECT_EQ(TitleOf(*child->GetGroup()), u"Compare laptops");
  EXPECT_EQ(controller()->DepthFor(child), 0);
  EXPECT_EQ(parent->GetGroup(), task);
  EXPECT_EQ(sibling->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->headline(), u"Created task Compare laptops");
  EXPECT_TRUE(bar()->card()->offers_undo());
  // The request did carry the name check, and its answer was to keep the name.
  EXPECT_NE(Requests()[3].body.find("needs a new name"), std::string::npos);

  bar()->Undo();
  EXPECT_EQ(child->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(child), 1);
}

// Without the fixture's careful values, uw asks for less before it acts,
// and each kind of answer has its own threshold.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       EachThresholdSetsHowSureOneKindOfAnswerMustBe) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.75));

  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->card()->headline(), u"Suggested Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.75 of 0.90");

  prefs()->ClearPref(kOrganizationMoveThresholdPref);
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.75 of 0.70");

  // Starting a task still asks for the fixture's 0.75.
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.6));
  tabs::TabInterface* other = OpenLinkFrom(ungrouped, "laptops");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_FALSE(other->GetGroup());
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.60 of 0.75");

  prefs()->ClearPref(kOrganizationNewTaskThresholdPref);
  Browse(other, "desks");
  ASSERT_TRUE(SettleAfter(4));
  ASSERT_TRUE(other->GetGroup());
  EXPECT_NE(other->GetGroup(), task);
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.60 of 0.55");

  SaveThresholds(prefs(), {0.50, 0.95, 0.95});
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  tabs::TabInterface* last = OpenLinkFrom(ungrouped, "flights");
  ASSERT_TRUE(SettleAfter(5));
  EXPECT_EQ(last->GetGroup(), task);
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.50 of 0.50");
}

// The threshold for a new name is its own. Here uw would neither move a tab
// nor start a task on these answers, and still renames the task.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheRenameThresholdIsItsOwn) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Compare Baixa hotels"));
  tabs::TabInterface* first = OpenLinkFrom(InitialTab(), "baixa");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = first->GetGroup();
  ASSERT_TRUE(task);

  SaveThresholds(prefs(), {0.95, 0.95, 0.60});
  Answer("/v1/systemone", SystemOneAnswer("none", 0.9, 0.9, 0.65));
  Answer("/v1/chat/completions", ChatAnswer("Compare Lisbon hotels"));
  tabs::TabInterface* second = OpenLinkFrom(first, "chiado");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_EQ(second->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Compare Lisbon hotels");
  EXPECT_EQ(bar()->card()->headline(), u"Naming the task");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.65 of 0.60");
}

// Web apps keep their state in the address. A tab is asked about again when
// its title changes or it goes to another site, not for a new path or new
// parameters under the same title.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       OnlyANewTitleOrAnotherSiteAsksAgain) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  const auto nothing_sent = [&] {
    organizer()->DispatchNowForTesting();
    WaitForPageText(link);
    return Requests().size() == 1u && organizer()->pending_requests() == 0u;
  };

  // The page changes its own address, as a filter or a sort order does.
  ASSERT_TRUE(content::ExecJs(
      link->GetContents(),
      "history.pushState({}, '', '/page/lisbon?sort=price#top')"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(nothing_sent());

  // The same page loads again with other parameters, and then from another
  // path.
  Browse(link, "lisbon?sort=rating");
  EXPECT_TRUE(nothing_sent());
  Browse(link, "alfama?title=lisbon");
  EXPECT_TRUE(nothing_sent());

  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(Requests().size(), 2u);
  // The request carries the address the tab is at now.
  EXPECT_NE(Requests()[1].body.find("/page/hotels"), std::string::npos);

  // The same title on another site is another page.
  content::WebContents* contents = link->GetContents();
  contents->GetController().LoadURL(
      embedded_test_server()->GetURL("other.test", "/page/hotels"),
      content::Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_EQ(Requests().size(), 3u);

  // Each line of the log says which tab it was about.
  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(*lines[0].FindString("page"), Id(link).value());
  EXPECT_EQ(*lines[2].FindString("page"), Id(link).value());
}

// A pinned tab is not in the tab tree, so the organizer leaves it alone.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       APinnedTabIsNeverAskedAbout) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.99));

  // Pinning a tab drops the request that was out for it.
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  tabs()->SetTabPinned(tabs()->GetIndexOfTab(link), true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return organizer()->pending_requests() == 0; }));
  EXPECT_TRUE(link->IsPinned());
  EXPECT_FALSE(link->GetGroup());
  const size_t sent = Requests().size();

  Browse(link, "hotels");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_EQ(Requests().size(), sent);
  EXPECT_EQ(organizer()->pending_requests(), 0u);
  EXPECT_TRUE(link->IsPinned());
  EXPECT_FALSE(link->GetGroup());

  // A tab opened from it is an ordinary tab.
  tabs::TabInterface* opened = OpenLinkFrom(link, "flights");
  ASSERT_TRUE(SettleAfter(sent + 1));
  EXPECT_EQ(opened->GetGroup(), task);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AConfirmedNewTaskIsNamedByTheLanguageModel) {
  UseDecisionModel(DecisionProvider::kJev);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.6));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->card()->headline(), u"Suggested a new task");
  EXPECT_EQ(bar()->card()->confidence(), u"Confidence 0.60 of 0.75");
  ASSERT_TRUE(bar()->card()->offers_accept());

  bar()->AcceptSuggestion();
  ASSERT_TRUE(SettleAfter(2));
  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->headline(), u"Created task Plan Lisbon trip");
  EXPECT_TRUE(bar()->card()->offers_undo());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ASuggestionForAChangedTabIsNotCarriedOut) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(DecisionProvider::kOpenAiCompatible);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_TRUE(bar()->card()->offers_accept());

  Browse(link, "weather");
  bar()->AcceptSuggestion();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_EQ(bar()->card()->headline(), u"Ignored: the tab changed");
  EXPECT_FALSE(bar()->card()->offers_accept());
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
  ASSERT_TRUE(bar()->card_visible());

  ASSERT_TRUE(controller()->MoveToScratchpad({Id(link)}));
  EXPECT_FALSE(bar()->card_visible());
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
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.99));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_TRUE(Requests().empty());
  EXPECT_FALSE(link->GetGroup());
}

}  // namespace
}  // namespace uw
