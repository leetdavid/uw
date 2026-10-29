// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/tab_tools.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/pins/pin_store.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_shell.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::UnorderedElementsAre;
using Kind = AgentLoop::Step::Kind;

// Serves /page/<title> as a page with that title.
std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  constexpr std::string_view kPage = "/page/";
  if (!request.relative_url.starts_with(kPage)) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(base::StrCat(
      {"<title>",
       base::UnescapeBinaryURLComponent(
           std::string_view(request.relative_url).substr(kPage.size())),
       "</title>"}));
  return response;
}

// A terminal whose shell is running a command.
class BusyShell : public TerminalShell {
 public:
  explicit BusyShell(content::WebContents* contents) : contents_(contents) {
    Set(contents_, this);
  }
  ~BusyShell() override { Unset(contents_, this); }

  void Write(const std::string& text) override {}
  void GetActivity(
      base::OnceCallback<void(ShellHost::Activity)> done) override {
    ShellHost::Activity activity;
    activity.program = "npm";
    std::move(done).Run(activity);
  }
  void Read(int from, ReadDone done) override { std::move(done).Run("", 0); }
  base::TimeTicks last_output() const override { return {}; }

 private:
  raw_ptr<content::WebContents> contents_;
};

class TabToolsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    chat_ = tabs()->GetActiveTab();
    tools_ = std::make_unique<AgentTools>(*chat_);
  }

  void TearDownOnMainThread() override {
    agent_.reset();
    tools_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  TabOrganizationController* tree(Browser* window = nullptr) {
    return TabOrganizationController::From(window ? window : browser());
  }

  // Opens a tab of the user's with a page titled `title` and returns it.
  tabs::TabInterface* Open(std::string_view title, Browser* window = nullptr) {
    window = window ? window : browser();
    content::WebContents* page = chrome::AddAndReturnTabAt(
        window, embedded_test_server()->GetURL(base::StrCat({"/page/", title})),
        -1, /*foreground=*/false);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    base::RunLoop().RunUntilIdle();
    return window->tab_strip_model()->GetTabForWebContents(page);
  }

  std::string IdOf(tabs::TabInterface* tab) {
    return ShortId(
        TabOrganizationController::From(tab->GetBrowserWindowInterface())
            ->IdFor(tab)
            ->value());
  }

  // Starts a task named `name` holding `tabs` and returns its id.
  std::string Task(std::vector<tabs::TabInterface*> members,
                   std::u16string name) {
    std::vector<PageId> ids;
    for (tabs::TabInterface* tab : members) {
      ids.push_back(*tree()->IdFor(tab));
    }
    const std::optional<tab_groups::TabGroupId> group =
        tree()->MoveToNewTask(ids, std::move(name));
    EXPECT_TRUE(group);
    return ShortId(tree()->KeyOf(*group));
  }

  base::DictValue Call(std::string_view tool, base::DictValue args) {
    base::test::TestFuture<base::DictValue> done;
    tools_->Call(tool, args, done.GetCallback());
    return done.Take();
  }

  AgentTools::TabCheck Check(std::string_view tool, base::DictValue args) {
    base::test::TestFuture<AgentTools::TabCheck> done;
    tools_->CheckTabStep(tool, args, done.GetCallback());
    return done.Take();
  }

  static base::DictValue Tabs(std::vector<std::string> ids) {
    base::ListValue list;
    for (std::string& id : ids) {
      list.Append(std::move(id));
    }
    return base::DictValue().Set("tabs", std::move(list));
  }

  bool IsOpen(tabs::TabInterface* tab) {
    for (int i = 0; i < tabs()->count(); ++i) {
      if (tabs()->GetTabAtIndex(i) == tab) {
        return true;
      }
    }
    return false;
  }

  // A chat's agent on a scripted model, in `mode`, that approves or
  // declines as `answer` says and lists what it asked.
  void StartAgent(ApprovalMode mode,
                  AgentLoop::ApprovalAnswer::Choice answer =
                      AgentLoop::ApprovalAnswer::Choice::kApprove) {
    AgentModel model;
    model.connection.endpoint = embedded_test_server()->GetURL("/v1").spec();
    model.connection.format = ProviderFormat::kChatCompletions;
    model.connection.model = "test-agent";
    agent_ = std::make_unique<AgentLoop>(
        *chat_, std::move(model),
        base::BindLambdaForTesting([this](const AgentLoop::Step& step) {
          AgentLoop::Step copy;
          copy.kind = step.kind;
          copy.tool = step.tool;
          copy.result = step.result.Clone();
          steps_.push_back(std::move(copy));
        }));
    agent_->SetMode(mode);
    agent_->SetApprover(base::BindLambdaForTesting(
        [this, answer](
            AgentTools::Approval approval,
            base::OnceCallback<void(AgentLoop::ApprovalAnswer)> done) {
          asked_.push_back(approval.action);
          std::move(done).Run({answer});
        }));
  }

  void Ask(std::string_view text) {
    steps_.clear();
    agent_->Send(text);
    ASSERT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));
  }

  raw_ptr<tabs::TabInterface> chat_;
  std::unique_ptr<AgentTools> tools_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<AgentLoop> agent_;
  std::vector<AgentLoop::Step> steps_;
  std::vector<std::string> asked_;
};

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest, ListsTheUsersTabsByTaskWithIds) {
  tabs::TabInterface* booking = Open("TAP booking");
  tabs::TabInterface* kayak = Open("KAYAK");
  tabs::TabInterface* news = Open("Hacker News");
  const std::string flights = Task({booking, kayak}, u"Flights");

  const base::DictValue listed = Call(kTabsTool, base::DictValue());

  ASSERT_FALSE(listed.FindString("error"));
  EXPECT_EQ(listed.FindInt("tabs"), 4);
  EXPECT_EQ(listed.FindInt("workspaces"), 1);
  const std::string& list = *listed.FindString("list");
  EXPECT_THAT(list,
              HasSubstr(base::StrCat({" › Flights (task ", flights, ")"})));
  EXPECT_THAT(list, HasSubstr(base::StrCat(
                        {"  ", IdOf(booking), "  TAP booking · 127.0.0.1"})));
  EXPECT_THAT(list,
              HasSubstr(base::StrCat({"  ", IdOf(news), "  Hacker News"})));
  EXPECT_THAT(list, HasSubstr("this chat"));
  // Nothing is read from the pages.
  EXPECT_THAT(list, Not(HasSubstr("<title>")));

  const base::DictValue found =
      Call(kTabsTool, base::DictValue().Set("query", "kayak"));
  EXPECT_EQ(found.FindInt("tabs"), 1);
  EXPECT_EQ(
      Call(kTabsTool, base::DictValue().Set("task", "flights")).FindInt("tabs"),
      2);
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       AMoveToANewTaskIsTheUsersAndUndoPutsItBack) {
  tabs::TabInterface* memmo = Open("Memmo Alfama");
  tabs::TabInterface* flat = Open("Alfama flat");

  const AgentTools::TabCheck check = Check(
      kMoveTool, Tabs({IdOf(memmo), IdOf(flat)}).Set("new_task", "Hotels"));
  ASSERT_FALSE(check.refused);
  ASSERT_TRUE(check.approval);
  EXPECT_FALSE(check.closes);
  EXPECT_EQ(check.approval->action, "Move 2 tabs to a new task, Hotels");
  EXPECT_EQ(check.approval->tabs.size(), 2u);

  const base::DictValue moved = Call(
      kMoveTool, Tabs({IdOf(memmo), IdOf(flat)}).Set("new_task", "Hotels"));

  ASSERT_FALSE(moved.FindString("error"));
  EXPECT_EQ(moved.FindBool("new"), true);
  EXPECT_EQ(*moved.FindStringByDottedPath("task.name"), "Hotels");
  const std::string task = tree()->tree().Find(*tree()->IdFor(memmo))->task;
  EXPECT_FALSE(task.empty());
  EXPECT_EQ(tree()->tree().Find(*tree()->IdFor(flat))->task, task);
  // A name the agent gives is the user's: uw never replaces it.
  EXPECT_TRUE(tree()->HasManualName(task));

  ASSERT_TRUE(tree()->CanUndo());
  tree()->Undo();
  EXPECT_TRUE(tree()->tree().Find(*tree()->IdFor(memmo))->task.empty());
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       ClosingNamesTheTabsAndClosesEachWithItsBranch) {
  tabs::TabInterface* skyscanner = Open("Skyscanner");
  tabs::TabInterface* expedia = Open("Expedia");
  tabs::TabInterface* booking = Open("TAP booking");
  Task({skyscanner, expedia, booking}, u"Flights");

  const AgentTools::TabCheck check =
      Check(kCloseTool, Tabs({IdOf(skyscanner), IdOf(expedia)}));
  ASSERT_TRUE(check.approval);
  EXPECT_TRUE(check.closes);
  EXPECT_EQ(check.approval->action, "Close 2 tabs in Flights");
  ASSERT_EQ(check.approval->tabs.size(), 2u);
  EXPECT_THAT((std::vector<std::string>{check.approval->tabs[0].title,
                                        check.approval->tabs[1].title}),
              UnorderedElementsAre("Skyscanner", "Expedia"));
  EXPECT_EQ(check.approval->reason,
            "Closed tabs come back from History › Recently Closed.");

  const base::DictValue closed =
      Call(kCloseTool, Tabs({IdOf(skyscanner), IdOf(expedia)}));

  ASSERT_FALSE(closed.FindString("error"));
  EXPECT_EQ(closed.FindList("closed")->size(), 2u);
  EXPECT_EQ(*closed.FindStringByDottedPath("task.name"), "Flights");
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsOpen(skyscanner); }));
  EXPECT_TRUE(IsOpen(booking));
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       TheAgentsOwnTabsCloseWithoutAsking) {
  tabs::TabInterface* mine = nullptr;
  {
    base::test::TestFuture<base::DictValue> opened;
    tools_->Call(
        "open",
        base::DictValue().Set(
            "url", embedded_test_server()->GetURL("/page/Mine").spec()),
        opened.GetCallback());
    // The engine acts only on https pages; the tab opens all the same.
    EXPECT_TRUE(opened.Wait());
    mine = tools_->OpenTabs()[1];
  }
  ASSERT_TRUE(mine);

  const base::DictValue listed = Call(kTabsTool, base::DictValue());
  EXPECT_THAT(*listed.FindString("list"), HasSubstr("your tab 1"));
  const AgentTools::TabCheck check = Check(
      kCloseTool, base::DictValue().Set("tabs", base::ListValue().Append(1)));
  EXPECT_FALSE(check.refused);
  EXPECT_FALSE(check.approval);
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       ABusyTerminalAPinAndTheChatAreNotClosed) {
  tabs::TabInterface* terminal = Open("shop-web");
  BusyShell shell(terminal->GetContents());
  const base::DictValue busy = Call(kCloseTool, Tabs({IdOf(terminal)}));
  EXPECT_EQ(*busy.FindString("refused"), "a command runs there");
  EXPECT_EQ(*busy.FindString("error"),
            "Didn't close shop-web: a command runs there.");
  EXPECT_TRUE(IsOpen(terminal));
  EXPECT_THAT(*Call(kTabsTool, base::DictValue()).FindString("list"),
              HasSubstr("shop-web · 127.0.0.1 · busy"));

  EXPECT_EQ(*Call(kCloseTool, Tabs({IdOf(chat_)})).FindString("refused"),
            "it's this chat's own tab");

  PinStore::For(browser()->GetProfile())
      ->Insert(0, embedded_test_server()->GetURL("/page/Mail"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tabs()->IndexOfFirstNonPinnedTab() == 1; }));
  const std::string pin =
      PinSync::PinFor(tabs()->GetTabAtIndex(0))->id.substr(0, kShortIdLength);
  EXPECT_THAT(*Call(kTabsTool, base::DictValue()).FindString("list"),
              HasSubstr("Pinned\n  "));
  EXPECT_EQ(*Call(kCloseTool, Tabs({pin})).FindString("refused"),
            "it's a pinned tab");
  EXPECT_EQ(*Call(kMoveTool, Tabs({pin}).Set("scratchpad", true))
                 .FindString("refused"),
            "it's a pinned tab");
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       AnIdThatIsNotUniqueOrTabsInTwoWindowsAreRefused) {
  tabs::TabInterface* here = Open("Here");
  Browser* second = CreateBrowser(browser()->GetProfile());
  tabs::TabInterface* there = Open("There", second);

  const base::DictValue moved =
      Call(kMoveTool, Tabs({IdOf(here), IdOf(there)}).Set("new_task", "Both"));
  EXPECT_EQ(*moved.FindString("refused"), "they're in different windows");

  const base::DictValue none =
      Call(kSelectTool, base::DictValue().Set("tab", "zz"));
  EXPECT_EQ(*none.FindString("refused"), "no tab has that id");
}

IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest,
                       SelectShowsATabAndRenameLocksTheName) {
  tabs::TabInterface* kayak = Open("KAYAK");
  const std::string task = Task({kayak}, u"New task");
  ASSERT_NE(tabs()->GetActiveTab(), kayak);

  const base::DictValue shown =
      Call(kSelectTool, base::DictValue().Set("tab", IdOf(kayak)));
  ASSERT_FALSE(shown.FindString("error"));
  EXPECT_EQ(tabs()->GetActiveTab(), kayak);

  const AgentTools::TabCheck check =
      Check(kRenameTaskTool,
            base::DictValue().Set("task", task).Set("name", "Trip to Lisbon"));
  ASSERT_TRUE(check.approval);
  EXPECT_EQ(check.approval->action, "Rename New task to Trip to Lisbon");
  EXPECT_EQ(check.approval->new_name, "Trip to Lisbon");

  const base::DictValue renamed =
      Call(kRenameTaskTool,
           base::DictValue().Set("task", task).Set("name", "Trip to Lisbon"));
  ASSERT_FALSE(renamed.FindString("error"));
  EXPECT_EQ(*renamed.FindString("was"), "New task");
  const std::string key = tree()->tree().Find(*tree()->IdFor(kayak))->task;
  EXPECT_EQ(tree()->TaskTitle(key), u"Trip to Lisbon");
  EXPECT_TRUE(tree()->HasManualName(key));
}

// Auto asks before a close and not before a move. Full auto never asks, and
// Ask first asks before a move too.
IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest, EachModeAsksAsThePlanSays) {
  tabs::TabInterface* a = Open("Alpha");
  tabs::TabInterface* b = Open("Beta");
  const auto move = [&](std::string_view name) {
    return ScriptedModel::Calls(base::ListValue().Append(
        ScriptedModel::Call(kMoveTool, Tabs({IdOf(a)}).Set("new_task", name))));
  };
  const auto close = [&](tabs::TabInterface* tab) {
    return ScriptedModel::Calls(base::ListValue().Append(
        ScriptedModel::Call(kCloseTool, Tabs({IdOf(tab)}))));
  };

  StartAgent(ApprovalMode::kAuto);
  model_.Script({move("Greek"), close(b), ScriptedModel::Answer("Done.")});
  Ask("Tidy up");
  EXPECT_THAT(asked_, ElementsAre("Close 1 tab in the Scratchpad"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsOpen(b); }));

  asked_.clear();
  StartAgent(ApprovalMode::kAskFirst,
             AgentLoop::ApprovalAnswer::Choice::kDecline);
  model_.Script({move("Letters"), ScriptedModel::Answer("You said no.")});
  Ask("Tidy up again");
  EXPECT_THAT(asked_, ElementsAre(HasSubstr("Move 1 tab to")));
  EXPECT_THAT(*steps_[0].result.FindString("error"),
              HasSubstr("The user did not approve"));

  asked_.clear();
  StartAgent(ApprovalMode::kFullAuto);
  model_.Script({close(a), ScriptedModel::Answer("Closed.")});
  Ask("Close it");
  EXPECT_TRUE(asked_.empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsOpen(a); }));
}

// A run has nobody to show a tab to, so it is not offered select, and a call
// to it does nothing.
IN_PROC_BROWSER_TEST_F(TabToolsBrowserTest, ARunHasNoSelect) {
  tabs::TabInterface* kayak = Open("KAYAK");
  StartAgent(ApprovalMode::kAuto);
  agent_->SetReport(base::BindRepeating(
      [](const base::DictValue&) { return base::DictValue(); }));
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           kSelectTool, base::DictValue().Set("tab", IdOf(kayak))))),
       ScriptedModel::Answer("Nothing to show.")});
  Ask("Show me KAYAK");
  EXPECT_THAT(model_.Requests()[0], Not(HasSubstr("\"select\"")));
  EXPECT_THAT(model_.Requests()[0], HasSubstr("\"close_task\""));
  EXPECT_THAT(*steps_[0].result.FindString("error"),
              HasSubstr("no tool called select"));
  EXPECT_NE(tabs()->GetActiveTab(), kayak);
}

class SharedTabToolsBrowserTest : public TabToolsBrowserTest {
 public:
  SharedTabToolsBrowserTest() { features_.InitAndEnableFeature(kUwSharedTabs); }

 private:
  base::test::ScopedFeatureList features_;
};

// Every window on a workspace shows its tabs, and the list names each once.
IN_PROC_BROWSER_TEST_F(SharedTabToolsBrowserTest, EachTabIsListedOnce) {
  Open("Alpha");
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return second->tab_strip_model()->count() >= 2; }));

  const base::DictValue listed = Call(kTabsTool, base::DictValue());

  EXPECT_THAT(*listed.FindString("list"), Not(HasSubstr("(window 2)")));
  EXPECT_EQ(listed.FindInt("workspaces"), 1);
  const std::string& list = *listed.FindString("list");
  EXPECT_EQ(list.find("Alpha"), list.rfind("Alpha"));
}

}  // namespace
}  // namespace uw
