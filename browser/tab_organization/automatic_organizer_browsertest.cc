// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_command_line.h"
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
#include "components/os_crypt/common/os_crypt_switches.h"
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
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/organizer_log.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/name_lock.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/ui/views/tab_organization/task_card.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"
#include "uw/components/ai_providers/providers.h"
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

  // Answers the decision model's request about the tab titled `title`.
  void AnswerAbout(const std::string& title, std::string body) {
    models_.AnswerAbout("/v1/systemone", "Title: " + title + "\\n",
                        std::move(body));
  }

  // The body of the decision request about the tab titled `title`.
  std::string RequestAbout(const std::string& title) {
    for (const ModelRequest& request : Requests()) {
      if (request.path == "/v1/systemone" &&
          request.body.find("Title: " + title + "\\n") != std::string::npos) {
        return request.body;
      }
    }
    return std::string();
  }

  // Leaves requests to `path` without an answer.
  void Hold(const std::string& path) { models_.Hold(path); }

  void Fail(const std::string& path, int status) {
    models_.Fail(path, status, "{}");
  }

  std::vector<ModelRequest> Requests() { return models_.Requests(); }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  // Picks `model` at a provider at the test server that speaks `format`, as
  // uw://providers saves it, and turns automatic organization on.
  void UseModel(ModelSlot slot,
                ProviderFormat format,
                const std::string& model,
                const std::string& key) {
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = model;
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    provider.format = format;
    provider.id = AddProvider(*prefs(), provider);
    provider.key_account = KeyAccountFor(provider.id);
    ASSERT_TRUE(ApiKeyStore().SetKey(provider.key_account, key));
    UpdateProvider(*prefs(), provider);
    SetChoice(*prefs(), slot, provider.id, model);
    prefs()->SetBoolean(kOrganizationEnabledPref, true);
  }

  // Jev speaks System One; the OpenAI Decisions API and compatible gateways
  // speak Decisions.
  void UseDecisionModel(ProviderFormat format) {
    UseModel(ModelSlot::kDecision, format, "decision-model", "decision-key");
  }

  void UseLanguageModel() {
    UseModel(ModelSlot::kLanguage, ProviderFormat::kChatCompletions,
             "language-model", "language-key");
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

  // The lock on the row of the task `group`.
  NameLock* NameLockOf(const tab_groups::TabGroupId& group) {
    return FindOnRow<NameLock>(BrowserView::GetBrowserViewForBrowser(browser()),
                               group);
  }

  // The spinner on the row of the task `group`.
  NamingSpinner* SpinnerOf(const tab_groups::TabGroupId& group) {
    return FindOnRow<NamingSpinner>(
        BrowserView::GetBrowserViewForBrowser(browser()), group);
  }

  // Opens the card of the task `group` under its row, as the row's menu
  // button does.
  std::unique_ptr<views::Widget> OpenCardOf(
      const tab_groups::TabGroupId& group) {
    return TaskCard::Show(browser(), group, NameLockOf(group)->parent());
  }

  // The row `id` of an open task card, or null.
  static views::Button* CardRow(views::Widget* card, TaskCard::Id id) {
    return views::AsViewClass<views::Button>(
        card->widget_delegate()->GetContentsView()->GetViewByID(id));
  }

  // The name field on the row of the task `group`.
  TaskNameField* NameFieldOf(const tab_groups::TabGroupId& group) {
    return TaskNameField::On(NameLockOf(group)->parent());
  }

  // A window with one task named `name` that holds the pages "flights" and
  // "hotels". Automatic organization is paused, so only a request the user
  // asks for goes out.
  tab_groups::TabGroupId PausedTask(const std::u16string& name) {
    SetPauseState(prefs(), PauseMode::kIndefinite);
    Browse(InitialTab(), "flights");
    tabs::TabInterface* second = NewTab();
    Browse(second, "hotels");
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask({Id(InitialTab()), Id(second)}, name);
    CHECK(task);
    base::RunLoop().RunUntilIdle();
    return *task;
  }

  template <typename Mark>
  static Mark* FindOnRow(views::View* view,
                         const tab_groups::TabGroupId& group) {
    Mark* mark = views::AsViewClass<Mark>(view);
    if (mark && mark->group() == group) {
      return mark;
    }
    for (views::View* child : view->children()) {
      if (Mark* found = FindOnRow<Mark>(child, group)) {
        return found;
      }
    }
    return nullptr;
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  tabs::TabInterface* InitialTab() { return tabs()->GetTabAtIndex(0); }

  tabs::TabInterface* NewTab() {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    tabs::TabInterface* tab = tabs()->GetActiveTab();
    EXPECT_TRUE(content::WaitForLoadStop(tab->GetContents()));
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

  // Follows a link to `name` in `tab` itself.
  void FollowLink(tabs::TabInterface* tab, const std::string& name) {
    content::WebContents* contents = tab->GetContents();
    contents->GetController().LoadURL(
        embedded_test_server()->GetURL("/page/" + name), content::Referrer(),
        ui::PAGE_TRANSITION_LINK, std::string());
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

  // What typing a name in the task's editor does.
  void Rename(const tab_groups::TabGroupId& group,
              const std::u16string& title) {
    tabs()->ChangeTabGroupVisuals(
        group,
        tab_groups::TabGroupVisualData(
            title,
            tabs()->group_model()->GetTabGroup(group)->visual_data()->color()));
    base::RunLoop().RunUntilIdle();
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));

  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_EQ(controller()->DepthFor(link), 1);
  ASSERT_TRUE(SettleAfter(1));

  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(link), 0);
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
  EXPECT_EQ(bar()->card()->confidence(), u"");
  EXPECT_EQ(bar()->card()->source(), u"");
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
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));

  tabs::TabInterface* initial = InitialTab();
  tabs::TabInterface* link = OpenLinkFrom(initial, "lisbon");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Plan Lisbon trip");
  ASSERT_TRUE(SpinnerOf(*link->GetGroup()));
  EXPECT_FALSE(SpinnerOf(*link->GetGroup())->GetVisible());
  EXPECT_FALSE(initial->GetGroup());
  // The new task sits above the Scratchpad.
  EXPECT_EQ(tabs()->GetIndexOfTab(link), 0);
  EXPECT_EQ(tabs()->GetIndexOfTab(initial), 1);
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"Created task Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->confidence(), u"");
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
                       TheOrganizeMenuShowsTheUndoShortcut) {
  ui::Accelerator shortcut;
  ASSERT_TRUE(bar()->GetAcceleratorForCommandId(OrganizeBar::kUndo, &shortcut));
  EXPECT_EQ(shortcut,
            ui::Accelerator(ui::VKEY_Z, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN));
}

// A new task does not wait for its name. Its tabs move at once into a task
// called "New task", the row spins, and the card waits. A tab opened from it
// meanwhile lands in the task and asks for no second name. Undo takes the
// tabs back and drops the request for the name.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ANewTaskStartsBeforeItsName) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Hold("/v1/chat/completions");

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  organizer()->DispatchNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return Requests().size() == 2u; }));
  const std::optional<tab_groups::TabGroupId> task = link->GetGroup();
  ASSERT_TRUE(task);
  EXPECT_EQ(TitleOf(*task), u"New task");
  EXPECT_TRUE(organizer()->IsNaming(task->ToString()));
  ASSERT_TRUE(SpinnerOf(*task));
  EXPECT_TRUE(SpinnerOf(*task)->GetVisible());
  EXPECT_TRUE(SpinnerOf(*task)->spinning());
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_TRUE(bar()->card()->waiting());
  EXPECT_EQ(bar()->card()->headline(), u"Naming the task");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
  EXPECT_EQ(bar()->card()->source(), u"Asking language-model");
  SaveWindowScreenshot(browser(), "naming-a-new-task");

  tabs::TabInterface* opened = OpenLinkFrom(link, "hotels");
  ASSERT_EQ(opened->GetGroup(), task);
  organizer()->DispatchNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Requests().size() == 3u && organizer()->pending_requests() == 1u;
  }));
  EXPECT_EQ(Requests()[2].path, "/v1/systemone");
  EXPECT_EQ(opened->GetGroup(), task);

  // The page moves on before its name arrives. The task is still owed one,
  // so the next answer asks again, even about a one-off.
  AnswerAbout("weather", SystemOneAnswer("none", 0.99, 0.1));
  Browse(link, "weather");
  organizer()->DispatchNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Requests().size() == 5u && organizer()->pending_requests() == 1u;
  }));
  EXPECT_EQ(Requests()[4].path, "/v1/chat/completions");
  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_TRUE(organizer()->IsNaming(task->ToString()));
  EXPECT_TRUE(SpinnerOf(*task)->GetVisible());

  controller()->Undo();
  EXPECT_FALSE(link->GetGroup());
  EXPECT_FALSE(organizer()->IsNaming(task->ToString()));
  EXPECT_EQ(organizer()->pending_requests(), 0u);
  EXPECT_EQ(Requests().size(), 5u);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       DecisionModelAloneCreatesAnUnnamedTask) {
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
                       WithActivityTheCardShowsTheRequestAndThenItsAnswer) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
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
  EXPECT_EQ(shown, (std::vector{true, true}));
  EXPECT_EQ(waiting, (std::vector{true, false}));
  EXPECT_EQ(bar()->card()->headline(), u"Suggested Plan Lisbon trip");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");
}

// Without "Show organizer activity", the card says nothing while a model is
// asked or when it leaves a tab where it is. Of several failed requests in a
// row only the first shows, until a request works again.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ByDefaultTheCardShowsOnlyMovesAndTheFirstFailure) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Fail("/v1/decisions", 401);
  std::vector<bool> shown;
  const base::CallbackListSubscription events = organizer()->ObserveEvents(
      base::BindLambdaForTesting([&](const AutomaticOrganizer::Event&) {
        shown.push_back(bar()->card_visible());
      }));

  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_EQ(shown, (std::vector{false, true}));
  EXPECT_EQ(bar()->card()->headline(), u"The request failed: HTTP 401");
  EXPECT_EQ(bar()->card()->page(), u"lisbon");

  Browse(link, "museums");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(bar()->card()->page(), u"lisbon");

  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.5));
  Browse(link, "hotels");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_EQ(bar()->card()->page(), u"lisbon");

  Fail("/v1/decisions", 401);
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(4));
  ASSERT_TRUE(bar()->card_visible());
  EXPECT_EQ(bar()->card()->headline(), u"The request failed: HTTP 401");
  EXPECT_EQ(bar()->card()->page(), u"weather");
  EXPECT_EQ(bar()->card()->source(), u"");
}

// A sidebar full of tabs leaves the card exactly its own height. Nothing in
// it may be cut off, and it may not sit under the buttons around it.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheCardFitsInACrowdedSidebar) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kSystemOne);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(0);
  ASSERT_EQ(in_task->GetGroup(), task);
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  EXPECT_EQ(bar()->card()->confidence(), u"");
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
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.8));
  Answer("/v1/chat/completions", ChatAnswer("Compare laptops"));
  tabs::TabInterface* blank = NewTabBeside(tabs()->GetTabAtIndex(0));
  ASSERT_EQ(blank->GetGroup(), task);

  Browse(blank, "laptops");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(blank->GetGroup());
  EXPECT_NE(blank->GetGroup(), task);
  EXPECT_EQ(TitleOf(*blank->GetGroup()), u"Compare laptops");
  EXPECT_EQ(bar()->card()->headline(), u"Created task Compare laptops");
  EXPECT_EQ(bar()->card()->confidence(), u"");
}

// The case from the user's log: a search that has nothing to do with the
// task it sits in, and a result opened from it. Nobody asked about the
// search, as after a restart, so it is asked about before the result moves.
// Alone the search is a one-off. The result makes the two of them a task,
// and they leave together, with one request for a name.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ABranchThatFitsNoTaskLeavesTogether) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Research Warp"));
  tabs::TabInterface* warp = OpenLinkFrom(InitialTab(), "warp");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = warp->GetGroup();
  ASSERT_TRUE(task);
  Answer("/v1/systemone", SystemOneAnswer(task->ToString(), 0.95, 0.0, 0.9));
  tabs::TabInterface* ssh = OpenLinkFrom(warp, "ssh");
  ASSERT_TRUE(SettleAfter(3));
  ASSERT_EQ(ssh->GetGroup(), task);

  prefs()->SetBoolean(kOrganizationEnabledPref, false);
  tabs::TabInterface* search = NewTabBeside(warp);
  ASSERT_EQ(search->GetGroup(), task);
  Browse(search, "slippers");
  prefs()->SetBoolean(kOrganizationEnabledPref, true);

  AnswerAbout("slippers", SystemOneAnswer("none", 0.99, 0.2, 0.0));
  AnswerAbout("tpu", SystemOneAnswer("none", 0.99, 0.9, 0.0));
  Answer("/v1/chat/completions", ChatAnswer("Print slippers"));
  tabs::TabInterface* result = OpenLinkFrom(search, "tpu");
  ASSERT_EQ(result->GetGroup(), task);
  ASSERT_TRUE(SettleAfter(6));

  ASSERT_TRUE(search->GetGroup());
  EXPECT_NE(search->GetGroup(), task);
  EXPECT_EQ(result->GetGroup(), search->GetGroup());
  EXPECT_EQ(controller()->DepthFor(search), 0);
  EXPECT_EQ(controller()->DepthFor(result), 1);
  EXPECT_EQ(TitleOf(*search->GetGroup()), u"Print slippers");
  EXPECT_EQ(tabs()->group_model()->ListTabGroups().size(), 2u);
  // The new task comes after the older one.
  EXPECT_EQ(controller()->tree().Tasks().back(),
            search->GetGroup()->ToString());
  EXPECT_EQ(warp->GetGroup(), task);
  EXPECT_EQ(ssh->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Research Warp");
  EXPECT_EQ(bar()->card()->headline(), u"Created task Print slippers");
  EXPECT_EQ(bar()->card()->page(), u"slippers");
  // The result decided it: the search alone scored 0.20.
  EXPECT_EQ(bar()->card()->confidence(), u"");

  // The search was compared with the tabs that make up the task, not with
  // the result the model had put outside it.
  EXPECT_NE(RequestAbout("slippers")
                .find("The tab sits in a group with these tabs:\\n- warp\\n- "
                      "ssh\\nDecide whether this tab is part of the same piece "
                      "of work as those tabs."),
            std::string::npos);
  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 6u);
  EXPECT_EQ(requests[5].path, "/v1/chat/completions");
  EXPECT_NE(requests[5].body.find("Title: slippers"), std::string::npos);
  EXPECT_NE(requests[5].body.find("Other tabs in the task:\\n- tpu"),
            std::string::npos);
  EXPECT_NE(requests[3].body.find("Title: tpu"), std::string::npos);
  EXPECT_NE(requests[4].body.find("Title: slippers"), std::string::npos);
  EXPECT_THAT(Results("placement"),
              testing::ElementsAre("new_task", "left", "new_task", "created"));

  bar()->Undo();
  EXPECT_EQ(search->GetGroup(), task);
  EXPECT_EQ(result->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(result), 1);
}

// A page that goes on to something else leaves its task. A tab under it that
// still fits the task stays there, where the page was, and a tab under it
// that fits no task either goes along.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATabThatStillFitsItsTaskStaysBehind) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(0);
  ASSERT_EQ(in_task->GetGroup(), task);
  UseDecisionModel(ProviderFormat::kSystemOne);
  Answer("/v1/systemone", SystemOneAnswer(task.ToString(), 0.95));
  AnswerAbout("weather", SystemOneAnswer("none", 0.97, 0.1));
  AnswerAbout("radar", SystemOneAnswer("none", 0.6, 0.1));
  tabs::TabInterface* page = OpenLinkFrom(in_task, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  tabs::TabInterface* fits = OpenLinkFrom(page, "hotels");
  ASSERT_TRUE(SettleAfter(2));
  // The model is not sure about this one, so it stays for now.
  tabs::TabInterface* unsure = OpenLinkFrom(page, "radar");
  ASSERT_TRUE(SettleAfter(3));
  ASSERT_EQ(unsure->GetGroup(), task);
  ASSERT_EQ(controller()->DepthFor(fits), 2);

  Browse(page, "weather");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_FALSE(page->GetGroup());
  EXPECT_FALSE(unsure->GetGroup());
  EXPECT_EQ(controller()->DepthFor(unsure), 1);
  EXPECT_EQ(fits->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(fits), 1);
  EXPECT_EQ(bar()->card()->headline(), u"Moved to Scratchpad");
  EXPECT_EQ(bar()->card()->page(), u"weather");
  EXPECT_EQ(Requests().size(), 4u);

  bar()->Undo();
  EXPECT_EQ(page->GetGroup(), task);
  EXPECT_EQ(unsure->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(fits), 2);
}

// A feed is a hub: a page the user comes back to for many things. uw never
// makes one into a task. An article opened from it starts a task of its own,
// and the feed stays in the Scratchpad.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AFeedStaysWhenATabOpenedFromItStartsATask) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  AnswerAbout("feed", SystemOneAnswer("none", 0.99, 0.9, std::nullopt,
                                      /*hub=*/0.97));
  AnswerAbout("article", SystemOneAnswer("none", 0.99, 0.9, std::nullopt,
                                         /*hub=*/0.02));
  tabs::TabInterface* feed = InitialTab();
  Browse(feed, "feed");
  ASSERT_TRUE(SettleAfter(1));
  // The model said it starts a task. A hub does not.
  EXPECT_FALSE(feed->GetGroup());
  EXPECT_NE(Requests()[0].body.find("\"kind\""), std::string::npos);

  tabs::TabInterface* article = OpenLinkFrom(feed, "article");
  ASSERT_TRUE(SettleAfter(2));
  ASSERT_TRUE(article->GetGroup());
  EXPECT_FALSE(feed->GetGroup());
  EXPECT_EQ(controller()->DepthFor(article), 0);
  EXPECT_EQ(tabs()->group_model()->ListTabGroups().size(), 1u);
  EXPECT_EQ(Requests().size(), 2u);
}

// Tabs that were there before automatic organization came on were never
// asked about. Before their branch moves, they are.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TabsNobodyAskedAboutAreAskedBeforeTheirBranchMoves) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* page = OpenLinkFrom(in_task, "lisbon");
  tabs::TabInterface* fits = OpenLinkFrom(page, "hotels");
  tabs::TabInterface* leaves = OpenLinkFrom(page, "radar");
  ASSERT_EQ(leaves->GetGroup(), task);

  UseDecisionModel(ProviderFormat::kSystemOne);
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.1));
  AnswerAbout("hotels", SystemOneAnswer(task.ToString(), 0.95));
  organizer()->DispatchNowForTesting();
  WaitForPageText(page);
  ASSERT_TRUE(Requests().empty());

  Browse(page, "weather");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_FALSE(RequestAbout("hotels").empty());
  EXPECT_FALSE(RequestAbout("radar").empty());
  EXPECT_FALSE(page->GetGroup());
  EXPECT_FALSE(leaves->GetGroup());
  EXPECT_EQ(controller()->DepthFor(leaves), 1);
  EXPECT_EQ(fits->GetGroup(), task);
  EXPECT_EQ(Requests().size(), 3u);

  // A tab the user placed by hand is not asked about, and stays.
  bar()->Undo();
  ASSERT_EQ(page->GetGroup(), task);
  Browse(page, "forecast");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_FALSE(page->GetGroup());
  EXPECT_EQ(fits->GetGroup(), task);
  EXPECT_EQ(leaves->GetGroup(), task);
  EXPECT_EQ(Requests().size(), 4u);
}

// A task that still has the name every new task starts with tells the model
// nothing, so it is not offered, and its tabs are not taken out of it.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATaskWithoutANameIsNotOfferedAndKeepsItsTabs) {
  OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  TabOrganizationController::OpenNewTask(browser());
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
// in it starts a piece of work. A one-off names nothing. After an unusable
// name, the next tab in the task that changes asks again, one-off or not.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ATaskWithoutANameIsNamedWhenATabStartsIt) {
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  UseLanguageModel();
  bar()->ExecuteCommand(OrganizeBar::kShowActivity, 0);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.1));
  Answer("/v1/chat/completions", ChatAnswer(""));
  TabOrganizationController::OpenNewTask(browser());
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

  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.1));
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

// A task uw starts keeps the provisional name when the call for its name
// fails. The next change of a tab in the task asks for the name again, even a
// change to a one-off.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ANameThatFailedIsAskedForAgain) {
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  UseLanguageModel();
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.97));
  Fail("/v1/chat/completions", 401);

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = link->GetGroup();
  ASSERT_TRUE(task);
  EXPECT_EQ(TitleOf(*task), u"New task");

  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.1));
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(4));
  EXPECT_EQ(link->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Plan Lisbon trip");
}

// Every test and every scripted run of the app passes --use-mock-keychain.
// Such a run keeps its keys in memory: it neither reads nor replaces a key
// the user saved in the Keychain, which every profile on the Mac shares.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AMockKeychainRunLeavesTheUsersKeysAlone) {
  ApiKeyStore keys;
  const std::string account = KeyAccountFor("openai");
  {
    // The user's own browser runs without the switch.
    base::test::ScopedCommandLine their_run;
    their_run.GetProcessCommandLine()->RemoveSwitch(
        os_crypt::switches::kUseMockKeychain);
    ASSERT_TRUE(keys.SetKey(account, "their-key"));
  }

  EXPECT_FALSE(keys.GetKey(account));
  ASSERT_TRUE(keys.SetKey(account, "test-key"));
  EXPECT_EQ(keys.GetKey(account), "test-key");
  EXPECT_TRUE(keys.DeleteKey(account));
  EXPECT_FALSE(keys.GetKey(account));

  base::test::ScopedCommandLine their_run;
  their_run.GetProcessCommandLine()->RemoveSwitch(
      os_crypt::switches::kUseMockKeychain);
  EXPECT_EQ(keys.GetKey(account), "their-key");
  EXPECT_TRUE(keys.DeleteKey(account));
  EXPECT_FALSE(keys.GetKey(account));
}

// A tab that no longer matches the name of its task has either left the task
// or shows that the task has outgrown the name. The decision model says
// which, and only a name uw gave is replaced. A tab leaves only as a
// different piece of work from the task's other tabs, under any name.
IN_PROC_BROWSER_TEST_F(OrganizerLogBrowserTest,
                       ATaskThatOutgrowsItsNameIsRenamed) {
  UseDecisionModel(ProviderFormat::kSystemOne);
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
  EXPECT_EQ(Requests()[0].body.find("same piece of work"), std::string::npos);

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
                "The tab sits in a group with these tabs:\\n- baixa\\nDecide "
                "whether this tab is part of the same piece of work as those "
                "tabs."),
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

  // A name the user typed is theirs and is never replaced. The model is still
  // asked whether the tab is the same piece of work as the task's other tabs,
  // and one that is stays in the task.
  tabs()->ChangeTabGroupVisuals(
      *task,
      tab_groups::TabGroupVisualData(
          u"Lisbon",
          tabs()->group_model()->GetTabGroup(*task)->visual_data()->color()));
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.9, 0.99));
  Browse(second, "sintra");
  ASSERT_TRUE(SettleAfter(6));
  requests = Requests();
  ASSERT_EQ(requests.size(), 6u);
  EXPECT_NE(requests[5].body.find(
                "The tab sits in a group with these tabs:\\n- baixa"),
            std::string::npos);
  EXPECT_EQ(TitleOf(*task), u"Lisbon");
  EXPECT_EQ(second->GetGroup(), task);
  EXPECT_EQ(bar()->card()->headline(), u"Suggested a new task");

  // A different piece of work leaves.
  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.9, 0.05));
  Answer("/v1/chat/completions", ChatAnswer("Read about Sintra"));
  Browse(second, "pena");
  ASSERT_TRUE(SettleAfter(8));
  EXPECT_EQ(TitleOf(*task), u"Lisbon");
  ASSERT_TRUE(second->GetGroup());
  EXPECT_NE(second->GetGroup(), task);
  EXPECT_EQ(TitleOf(*second->GetGroup()), u"Read about Sintra");

  EXPECT_THAT(
      Results("placement"),
      testing::ElementsAre("new_task", "name", "left", "unsure", "new_task"));
  EXPECT_THAT(Results("name"), testing::ElementsAre("named", "named", "named"));
  const std::vector<base::DictValue> lines = LogLines();
  ASSERT_EQ(lines.size(), 8u);
  EXPECT_EQ(lines[2].FindDouble("new_name"), 0.93);
  EXPECT_EQ(lines[2].FindDouble("needed"), 0.90);
  EXPECT_EQ(lines[4].FindDouble("new_name"), 0.89);
  EXPECT_EQ(lines[5].FindDouble("new_name"), 0.99);
  EXPECT_EQ(lines[6].FindDouble("new_name"), 0.05);
}

// A lock on a task's row marks a name the user typed. Nothing else has one:
// not a name uw gave, not the name every new task starts with, and no task
// while automatic organization is off.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OnlyANameTheUserTypedIsLocked) {
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  TabOrganizationController::OpenNewTask(browser());
  base::RunLoop().RunUntilIdle();
  const std::optional<tab_groups::TabGroupId> task =
      tabs()->GetActiveTab()->GetGroup();
  ASSERT_TRUE(task);
  ASSERT_TRUE(NameLockOf(*task));
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());

  Rename(*task, u"Taxes 2026");
  EXPECT_TRUE(NameLockOf(*task)->GetVisible());
  EXPECT_EQ(NameLockOf(*task)->GetTooltipText(),
            u"Named by you. Click to let uw rename this task.");
  SaveWindowScreenshot(browser(), "name-lock");

  prefs()->SetBoolean(kOrganizationEnabledPref, false);
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());
  prefs()->SetBoolean(kOrganizationEnabledPref, true);
  EXPECT_TRUE(NameLockOf(*task)->GetVisible());

  controller()->NameTask(task->ToString(), u"File taxes");
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());
}

// uw never replaces a locked name. A click on the lock hands the name back:
// the lock goes, the name stays, and uw renames the task once it has outgrown
// that name.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AClickOnTheLockHandsTheNameBack) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/chat/completions", ChatAnswer("Compare Baixa hotels"));
  tabs::TabInterface* first = OpenLinkFrom(InitialTab(), "baixa");
  ASSERT_TRUE(SettleAfter(2));
  const std::optional<tab_groups::TabGroupId> task = first->GetGroup();
  ASSERT_TRUE(task);
  ASSERT_TRUE(NameLockOf(*task));
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());

  Rename(*task, u"Lisbon");
  EXPECT_TRUE(NameLockOf(*task)->GetVisible());
  Answer("/v1/systemone", SystemOneAnswer(task->ToString(), 0.99));
  tabs::TabInterface* second = OpenLinkFrom(first, "chiado");
  ASSERT_TRUE(SettleAfter(3));
  EXPECT_EQ(TitleOf(*task), u"Lisbon");

  views::test::ButtonTestApi(NameLockOf(*task)).NotifyDefaultMouseClick();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());
  EXPECT_EQ(TitleOf(*task), u"Lisbon");

  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.9, 0.95));
  Answer("/v1/chat/completions", ChatAnswer("Compare Lisbon hotels"));
  Browse(second, "alfama");
  ASSERT_TRUE(SettleAfter(5));
  EXPECT_NE(Requests()[3].body.find("same piece of work"), std::string::npos);
  EXPECT_EQ(second->GetGroup(), task);
  EXPECT_EQ(TitleOf(*task), u"Compare Lisbon hotels");
  EXPECT_FALSE(NameLockOf(*task)->GetVisible());
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

  UseDecisionModel(ProviderFormat::kSystemOne);
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

  // So the restored name can be replaced, and the user's cannot. Both new
  // tabs are compared with the tabs of their task, and the user's keeps a tab
  // that is the same piece of work under the name they typed.
  tabs::TabInterface* restored = nullptr;
  tabs::TabInterface* own = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
    (tab->GetGroup() == named ? restored : own) = tab;
  }
  ASSERT_TRUE(restored);
  ASSERT_TRUE(own);
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseLanguageModel();
  Answer("/v1/systemone", SystemOneAnswer(named->ToString(), 0.95, 0.0, 0.1));
  AnswerAbout("minutes", SystemOneAnswer("none", 0.97, 0.9, 0.95));
  tabs::TabInterface* alfama = OpenLinkFrom(restored, "alfama");
  tabs::TabInterface* minutes = OpenLinkFrom(own, "minutes");
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
  EXPECT_NE(
      request_about("alfama").find("The tab sits in a group with these tabs:"),
      std::string::npos);
  EXPECT_NE(request_about("minutes").find(
                "The tab sits in a group with these tabs:\\n- notes"),
            std::string::npos);
  ASSERT_TRUE(SettleAfter(Requests().size()));
  EXPECT_EQ(alfama->GetGroup(), named);
  EXPECT_EQ(minutes->GetGroup(), mine);
  EXPECT_EQ(TitleOf(*mine), u"Mine");
}

// A tab opened from another tab starts in that tab's task, under it. When it
// turns out to be a piece of work of its own, it leaves for a task of its
// own, since the name check calls it another piece of work.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AChildTabBreaksOutOfItsParentsTask) {
  UseDecisionModel(ProviderFormat::kSystemOne);
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

  Answer("/v1/systemone", SystemOneAnswer("none", 0.97, 0.8, 0.05));
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
  // The request did carry the name check, and its answer was that the tab is
  // another piece of work.
  EXPECT_NE(Requests()[3].body.find("same piece of work"), std::string::npos);

  bar()->Undo();
  EXPECT_EQ(child->GetGroup(), task);
  EXPECT_EQ(controller()->DepthFor(child), 1);
}

// Without the fixture's careful values, uw asks for less before it acts,
// and each kind of answer has its own threshold.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       EachThresholdSetsHowSureOneKindOfAnswerMustBe) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kSystemOne);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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

// On one site a tab in a task stays the same piece of work: following links
// and reloading ask nothing. An address the user typed asks again.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATabInATaskIsAskedAgainOnlyForSomethingNew) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.99));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_EQ(link->GetGroup(), task);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99));
  const auto nothing_sent = [&] {
    organizer()->DispatchNowForTesting();
    WaitForPageText(link);
    return Requests().size() == 1u && organizer()->pending_requests() == 0u;
  };

  FollowLink(link, "radar");
  EXPECT_TRUE(nothing_sent());
  link->GetContents()->GetController().Reload(content::ReloadType::NORMAL,
                                              false);
  ASSERT_TRUE(content::WaitForLoadStop(link->GetContents()));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(nothing_sent());
  EXPECT_EQ(link->GetGroup(), task);

  Browse(link, "weather");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(link->GetGroup());
}

// In the Scratchpad a page that updates its own title, as an unread count
// does, keeps its address and asks nothing. A new title at a new address asks
// again.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AScratchpadTabIsAskedAgainForANewTitleAtANewAddress) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "inbox");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_FALSE(link->GetGroup());

  ASSERT_TRUE(
      content::ExecJs(link->GetContents(), "document.title = 'inbox (3)'"));
  base::RunLoop().RunUntilIdle();
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_EQ(Requests().size(), 1u);
  EXPECT_EQ(organizer()->pending_requests(), 0u);

  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.99));
  FollowLink(link, "lisbon");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_EQ(link->GetGroup(), task);
}

// A hub opened in a task, such as a chat assistant's home page, stays there.
// It shows what it is for once its title and address change, and is asked
// about again then.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest, AHubStaysInItsTask) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kSystemOne);
  AnswerAbout("assistant", SystemOneAnswer("none", 0.99, 0.9, std::nullopt,
                                           /*hub=*/0.97));
  AnswerAbout("postcodes", SystemOneAnswer("none", 0.99, 0.1, std::nullopt,
                                           /*hub=*/0.02));
  tabs::TabInterface* chat =
      OpenLinkFrom(tabs()->GetTabAtIndex(0), "assistant");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_EQ(chat->GetGroup(), task);

  FollowLink(chat, "postcodes");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(chat->GetGroup());
}

// A kept tab stays where the user left it. uw does not ask about it, and it
// stays behind when the page above it leaves. Keep here again, or a move by
// hand, lets it go.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AKeptTabStaysWhereTheUserLeftIt) {
  const auto [ungrouped, task] = OneTask();
  tabs::TabInterface* page = OpenLinkFrom(tabs()->GetTabAtIndex(0), "lisbon");
  tabs::TabInterface* kept = OpenLinkFrom(page, "assistant");
  ASSERT_EQ(kept->GetGroup(), task);
  ASSERT_EQ(controller()->DepthFor(kept), controller()->DepthFor(page) + 1);
  controller()->Execute(TabOrganizationController::kKeepHere, {Id(kept)});
  ASSERT_TRUE(controller()->IsKept(Id(kept)));

  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99));
  Browse(kept, "weather");
  Browse(page, "radar");
  ASSERT_TRUE(SettleAfter(1));
  EXPECT_FALSE(page->GetGroup());
  EXPECT_EQ(kept->GetGroup(), task);
  EXPECT_TRUE(RequestAbout("weather").empty());

  controller()->Execute(TabOrganizationController::kKeepHere, {Id(kept)});
  EXPECT_FALSE(controller()->IsKept(Id(kept)));
  Browse(kept, "forecast");
  ASSERT_TRUE(SettleAfter(2));
  EXPECT_FALSE(kept->GetGroup());

  controller()->Execute(TabOrganizationController::kKeepHere, {Id(kept)});
  ASSERT_TRUE(controller()->IsKept(Id(kept)));
  controller()->MoveToTask({Id(kept)}, task);
  EXPECT_EQ(kept->GetGroup(), task);
  EXPECT_FALSE(controller()->IsKept(Id(kept)));
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       PRE_AKeptTabIsKeptAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  Browse(InitialTab(), "notes");
  controller()->Execute(TabOrganizationController::kKeepHere,
                        {Id(InitialTab())});
  ASSERT_TRUE(controller()->IsKept(Id(InitialTab())));
  // Lets the session take the tab's data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AKeptTabIsKeptAfterARestart) {
  tabs::TabInterface* notes = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
    if (tab->GetContents()->GetLastCommittedURL().path() == "/page/notes" ||
        tab->GetContents()->GetVisibleURL().path() == "/page/notes") {
      notes = tab;
    }
  }
  ASSERT_TRUE(notes);
  EXPECT_TRUE(controller()->IsKept(Id(notes)));
}

// A pinned tab is not in the tab tree, so the organizer leaves it alone.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       APinnedTabIsNeverAskedAbout) {
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kSystemOne);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  Answer("/v1/decisions", DecisionsAnswer(task.ToString(), 0.95));
  tabs::TabInterface* link = OpenLinkFrom(ungrouped, "lisbon");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_TRUE(bar()->card_visible());

  ASSERT_TRUE(controller()->MoveToScratchpad({Id(link)}));
  EXPECT_FALSE(bar()->card_visible());
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OrganizeMenuUndoesPausesAndOpensSettings) {
  prefs()->SetBoolean(kOrganizationEnabledPref, true);
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
  tabs::TabInterface* in_task = tabs()->GetTabAtIndex(0);
  ASSERT_EQ(in_task->GetGroup(), task);
  tabs()->ActivateTabAt(1);
  const int before = tabs()->count();

  TabOrganizationController::OpenNewTask(browser());

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
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
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

// Name Task on a task's card asks the language model for a name from the
// tabs the task holds, while automatic organization is paused too. The card
// closes at once. The name replaces the one the user had typed and is uw's
// from then on.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       NameTaskNamesTheTaskFromItsTabs) {
  UseLanguageModel();
  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  const tab_groups::TabGroupId task = PausedTask(u"Lisbon");
  ASSERT_TRUE(NameLockOf(task)->GetVisible());

  std::unique_ptr<views::Widget> card = OpenCardOf(task);
  views::Button* name = CardRow(card.get(), TaskCard::kNameId);
  ASSERT_TRUE(name);
  EXPECT_TRUE(name->GetEnabled());
  views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return card->IsClosed(); }));

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TitleOf(task) == u"Plan Lisbon trip"; }));
  ASSERT_EQ(Requests().size(), 1u);
  EXPECT_EQ(Requests()[0].path, "/v1/chat/completions");
  EXPECT_NE(Requests()[0].body.find("Title: flights\\n"), std::string::npos);
  EXPECT_NE(Requests()[0].body.find("- hotels"), std::string::npos);
  EXPECT_TRUE(controller()->HasGeneratedName(task.ToString()));
  EXPECT_FALSE(NameLockOf(task)->GetVisible());
  EXPECT_EQ(organizer()->pending_requests(), 0u);
}

// The name comes with no window open. Meanwhile the task's row shows its
// spinner, the card offers no second Name Task, and none goes out.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       NameTaskRunsWithTheCardClosed) {
  UseLanguageModel();
  Hold("/v1/chat/completions");
  const tab_groups::TabGroupId task = PausedTask(u"Lisbon");
  {
    std::unique_ptr<views::Widget> card = OpenCardOf(task);
    views::test::ButtonTestApi(CardRow(card.get(), TaskCard::kNameId))
        .NotifyDefaultMouseClick();
    ASSERT_TRUE(base::test::RunUntil([&] { return card->IsClosed(); }));
  }
  EXPECT_TRUE(organizer()->IsNaming(task.ToString()));
  EXPECT_TRUE(SpinnerOf(task)->spinning());
  ASSERT_TRUE(base::test::RunUntil([&] { return Requests().size() == 1u; }));
  SaveWindowScreenshot(browser(), "name-task-window");

  std::unique_ptr<views::Widget> card = OpenCardOf(task);
  EXPECT_FALSE(CardRow(card.get(), TaskCard::kNameId)->GetEnabled());
  organizer()->AskForName(task.ToString());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(Requests().size(), 1u);
  EXPECT_EQ(TitleOf(task), u"Lisbon");
}

// Name Task is only offered with a language model to ask.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       NameTaskNeedsALanguageModel) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  const tab_groups::TabGroupId task = PausedTask(u"Lisbon");
  EXPECT_FALSE(organizer()->CanAskForName());
  std::unique_ptr<views::Widget> card = OpenCardOf(task);
  EXPECT_FALSE(CardRow(card.get(), TaskCard::kNameId));
  EXPECT_TRUE(CardRow(card.get(), TaskCard::kRenameId));
}

// When no name comes of Name Task, the card at the foot of the sidebar says
// so and why, even with Show Activity off, and the task keeps its name as
// the user's. Try Again asks again.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       AFailedNameTaskSaysSoAndTriesAgain) {
  UseLanguageModel();
  Fail("/v1/chat/completions", 401);
  prefs()->SetBoolean(kOrganizationShowActivityPref, false);
  const tab_groups::TabGroupId task = PausedTask(u"Lisbon");

  organizer()->AskForName(task.ToString());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return bar()->card()->GetVisible(); }));
  EXPECT_EQ(bar()->card()->headline(), u"Couldn't name Lisbon");
  EXPECT_EQ(bar()->card()->page(), u"The request failed: HTTP 401");
  EXPECT_EQ(bar()->card()->source(), u"");
  EXPECT_TRUE(bar()->card()->offers_retry());
  EXPECT_FALSE(bar()->card()->offers_undo());
  EXPECT_EQ(TitleOf(task), u"Lisbon");
  EXPECT_TRUE(controller()->HasManualName(task.ToString()));
  EXPECT_FALSE(organizer()->IsNaming(task.ToString()));
  SaveWindowScreenshot(browser(), "name-task-failed");

  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  bar()->AskForNameAgain(task.ToString());
  EXPECT_FALSE(bar()->card()->GetVisible());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TitleOf(task) == u"Plan Lisbon trip"; }));
  EXPECT_EQ(Requests().size(), 2u);
}

// Typing a name while Name Task waits drops the request: the user's name
// stays, and a late answer changes nothing.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TypingANameWinsOverNameTask) {
  UseLanguageModel();
  Hold("/v1/chat/completions");
  const tab_groups::TabGroupId task = PausedTask(u"Lisbon");
  organizer()->AskForName(task.ToString());
  ASSERT_TRUE(organizer()->IsNaming(task.ToString()));

  controller()->RenameOnItsRow(task.ToString());
  TaskNameField* field = NameFieldOf(task);
  ASSERT_TRUE(field);
  ASSERT_TRUE(base::test::RunUntil([&] { return field->editing(); }));
  EXPECT_FALSE(organizer()->IsNaming(task.ToString()));
  EXPECT_FALSE(SpinnerOf(task)->GetVisible());
  field->SetText(u"Lisbon in May");
  field->HandleKeyEvent(field, ui::KeyEvent(ui::EventType::kKeyPressed,
                                            ui::VKEY_RETURN, ui::EF_NONE));
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(TitleOf(task), u"Lisbon in May");
  EXPECT_TRUE(controller()->HasManualName(task.ToString()));

  Answer("/v1/chat/completions", ChatAnswer("Plan Lisbon trip"));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(TitleOf(task), u"Lisbon in May");
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       OrganizationOffSendsNothing) {
  UseDecisionModel(ProviderFormat::kOpenAiDecisions);
  prefs()->SetBoolean(kOrganizationEnabledPref, false);
  Answer("/v1/decisions", DecisionsAnswer("none", 0.99, 0.99));

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  organizer()->DispatchNowForTesting();
  WaitForPageText(link);
  EXPECT_TRUE(Requests().empty());
  EXPECT_FALSE(link->GetGroup());
}

// A terminal tab is filed like any tab. A request describes it by its title
// and its shell's folder, never by what the shell printed.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ATerminalTabIsFiledByItsTitleAndFolder) {
  // zsh finds none of the user's startup files there, so it sets no title.
  base::Environment::Create()->SetVar("ZDOTDIR", "/var/empty");
  const auto [ungrouped, task] = OneTask();
  UseDecisionModel(ProviderFormat::kSystemOne);
  Answer("/v1/systemone", SystemOneAnswer(task.ToString(), 0.97));

  chrome::ExecuteCommand(browser(), kNewTerminalCommand);
  tabs::TabInterface* terminal = tabs()->GetActiveTab();
  ASSERT_TRUE(content::WaitForLoadStop(terminal->GetContents()));
  // Once its shell is at a prompt, the tab is named after its folder.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return terminal->GetContents()->GetTitle() != u"Terminal"; }));
  const std::string name =
      base::UTF16ToUTF8(terminal->GetContents()->GetTitle());
  ASSERT_TRUE(SettleAfter(1));

  EXPECT_EQ(terminal->GetGroup(), task);
  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_NE(requests[0].body.find("Title: " + name), std::string::npos);
  EXPECT_NE(requests[0].body.find("Folder: ~"), std::string::npos);
  EXPECT_EQ(requests[0].body.find("Page excerpt"), std::string::npos);
}

// Before uw://providers, settings saved a language, a decision, and an agent
// connection. The first window of the build that has the page turns them
// into providers, and the organizer goes on with the same models.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       SavedConnectionsBecomeProvidersOnce) {
  ApiKeyStore keys;
  const std::string address = embedded_test_server()->GetURL("/v1").spec();
  ASSERT_TRUE(keys.SetKey("organization", "shared-key"));
  ASSERT_TRUE(keys.SetKey("agent", "shared-key"));
  ASSERT_TRUE(keys.SetKey("decision", "jev-key"));
  prefs()->SetInteger(kOrganizationModePref, 1);
  prefs()->SetString(kOrganizationEndpointPref, address);
  prefs()->SetString(kOrganizationModelPref, "language-model");
  prefs()->SetString(kOrganizationKeyAccountPref, "organization");
  prefs()->SetInteger(kAgentModePref, 1);
  prefs()->SetString(kAgentEndpointPref, address);
  prefs()->SetString(kAgentModelPref, "agent-model");
  prefs()->SetString(kAgentKeyAccountPref, "agent");
  prefs()->SetInteger(kDecisionProviderPref, 1);
  prefs()->SetString(kDecisionKeyAccountPref, "decision");
  prefs()->SetBoolean(kOrganizationEnabledPref, true);

  MoveConnectionsToProviders(prefs());

  // The language and agent connections shared an address and a key.
  const std::vector<Provider> providers = ReadProviders(*prefs());
  ASSERT_EQ(providers.size(), 2u);
  EXPECT_EQ(providers[0].preset, ProviderPreset::kCustom);
  EXPECT_EQ(providers[0].name, "127.0.0.1");
  EXPECT_EQ(providers[0].address, address);
  EXPECT_EQ(keys.GetKey(providers[0].key_account), "shared-key");
  EXPECT_EQ(providers[1].preset, ProviderPreset::kJev);
  EXPECT_EQ(keys.GetKey(providers[1].key_account), "jev-key");

  // Automatic organization goes on with the moved models at once.
  const ModelChoice language = *ReadChoice(*prefs(), ModelSlot::kLanguage);
  EXPECT_EQ(language.provider_id, providers[0].id);
  EXPECT_EQ(language.model, "language-model");
  const ModelChoice decision = *ReadChoice(*prefs(), ModelSlot::kDecision);
  EXPECT_EQ(decision.provider_id, providers[1].id);
  EXPECT_EQ(decision.model, "jev-latest");
  EXPECT_EQ(ReadChoice(*prefs(), ModelSlot::kAgent)->model, "agent-model");
  EXPECT_TRUE(OrganizationReady(prefs()));
  EXPECT_EQ(ReadDecisionModelConfig(prefs()).format,
            DecisionFormat::kSystemOne);
  EXPECT_EQ(AgentChoice(*prefs())->model, "agent-model");

  // The old connections and their Keychain accounts are gone, so a second
  // run changes nothing.
  EXPECT_EQ(prefs()->GetString(kOrganizationModelPref), "");
  EXPECT_EQ(prefs()->GetInteger(kDecisionProviderPref), 0);
  EXPECT_EQ(prefs()->GetString(kAgentEndpointPref), "");
  for (const char* account : {"organization", "agent", "decision"}) {
    EXPECT_FALSE(keys.GetKey(account)) << account;
  }
  EXPECT_EQ(prefs()->GetInteger(kUpgradeNoticePref),
            static_cast<int>(UpgradeNotice::kMerged));
  MoveConnectionsToProviders(prefs());
  EXPECT_EQ(ReadProviders(*prefs()).size(), 2u);
}

IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ANewWindowMovesTheSavedConnections) {
  prefs()->SetString(kOrganizationEndpointPref, "http://localhost:11434/v1");
  prefs()->SetString(kOrganizationModelPref, "llama3.2");

  CreateBrowser(browser()->GetProfile());

  const std::vector<Provider> providers = ReadProviders(*prefs());
  ASSERT_EQ(providers.size(), 1u);
  EXPECT_EQ(providers[0].preset, ProviderPreset::kOllama);
  EXPECT_EQ(providers[0].key_account, "");
  EXPECT_EQ(ReadChoice(*prefs(), ModelSlot::kLanguage)->model, "llama3.2");
  EXPECT_EQ(prefs()->GetInteger(kUpgradeNoticePref),
            static_cast<int>(UpgradeNotice::kMoved));
}

// A model picked on uw://providers is used at once, without a test.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ANewlyPickedModelIsUsedAtOnce) {
  Answer("/v1/chat/completions",
         ChatAnswer(R"({\"task\":\"none\",\"confidence\":0.9,)"
                    R"(\"new_task\":0.95,\"task_name\":\"Plan a trip\"})"));
  UseLanguageModel();
  const ModelChoice picked = *ReadChoice(*prefs(), ModelSlot::kLanguage);
  SetChoice(*prefs(), ModelSlot::kLanguage, picked.provider_id, "other-model");
  EXPECT_TRUE(OrganizationReady(prefs()));

  Browse(InitialTab(), "flights");
  ASSERT_TRUE(SettleAfter(1));
  ASSERT_FALSE(Requests().empty());
  EXPECT_NE(Requests()[0].body.find(R"("model":"other-model")"),
            std::string::npos);
}

// A language model at a provider that speaks Anthropic Messages gets the
// naming request in that format, and its answer names the task. A Custom
// provider sends the key both ways, as proxies read either.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       ALanguageModelOverAnthropicMessagesNamesTheNewTask) {
  UseDecisionModel(ProviderFormat::kSystemOne);
  UseModel(ModelSlot::kLanguage, ProviderFormat::kAnthropicMessages,
           "claude-model", "anthropic-key");
  Answer("/v1/systemone", SystemOneAnswer("none", 0.99, 0.97));
  Answer("/v1/messages", R"({"stop_reason":"end_turn","content":[)"
                         R"({"type":"text","text":"Plan Lisbon trip"}]})");

  tabs::TabInterface* link = OpenLinkFrom(InitialTab(), "lisbon");
  ASSERT_TRUE(SettleAfter(2));

  ASSERT_TRUE(link->GetGroup());
  EXPECT_EQ(TitleOf(*link->GetGroup()), u"Plan Lisbon trip");
  const std::vector<ModelRequest> requests = Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[1].path, "/v1/messages");
  EXPECT_EQ(requests[1].anthropic_key, "anthropic-key");
  EXPECT_EQ(requests[1].authorization, "Bearer anthropic-key");
  EXPECT_NE(requests[1].body.find(R"("model":"claude-model")"),
            std::string::npos);
  EXPECT_NE(requests[1].body.find(R"("system":)"), std::string::npos);
}

// Every label and button text in `view`.
void CollectTexts(const views::View* view, std::vector<std::u16string>& texts) {
  if (const auto* button = views::AsViewClass<views::LabelButton>(view)) {
    texts.emplace_back(button->GetText());
  } else if (const auto* label = views::AsViewClass<views::Label>(view)) {
    texts.emplace_back(label->GetText());
  } else if (const auto* styled =
                 views::AsViewClass<views::StyledLabel>(view)) {
    texts.emplace_back(styled->GetText());
  }
  for (const views::View* child : view->children()) {
    CollectTexts(child, texts);
  }
}

// A task's menu says "task" and never "group". It offers no Delete and no
// note that tasks save, since uw keeps no closed tasks.
IN_PROC_BROWSER_TEST_F(AutomaticOrganizerBrowserTest,
                       TheTaskMenuSaysTaskAndKeepsNoClosedTasks) {
  const auto [ungrouped, task] = OneTask();
  std::unique_ptr<views::Widget> menu = OpenCardOf(task);
  std::vector<std::u16string> texts;
  CollectTexts(menu->widget_delegate()->GetContentsView(), texts);
  EXPECT_THAT(texts, testing::IsSupersetOf({u"Rename Task", u"New Tab in Task",
                                            u"Move Task to New Window",
                                            u"Move Tabs to Scratchpad",
                                            u"Close Task"}));
  EXPECT_THAT(texts, testing::Not(testing::Contains(u"Delete Task")));
  for (const std::u16string& text : texts) {
    EXPECT_EQ(text.find(u"group"), std::u16string::npos) << text;
    EXPECT_EQ(text.find(u"Group"), std::u16string::npos) << text;
  }
}
}  // namespace
}  // namespace uw
