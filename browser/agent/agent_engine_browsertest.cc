// Copyright 2026 The uw Authors. All rights reserved.

// T43: uw drives Chromium's agent engine itself, without Gemini's panel.

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
#include "chrome/browser/actor/actor_task_metadata.h"
#include "chrome/browser/actor/enterprise_policy_checker.h"
#include "chrome/browser/actor/tab_observation_strategy.h"
#include "chrome/browser/actor/tools/click_tool_request.h"
#include "chrome/browser/actor/tools/navigate_tool_request.h"
#include "chrome/browser/actor/tools/type_tool_request.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/actor.mojom.h"
#include "components/actor/core/task_source_info.h"
#include "components/actor/public/mojom/actor_types.mojom.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/gfx/geometry/point.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {
namespace {

using actor::mojom::ActionResultCode;

// A field and a button that copies the field's text into a paragraph.
constexpr char kFormPage[] = R"(<!doctype html>
<title>Order form</title>
<style>
  #name, #send, #said { position: absolute; left: 20px; width: 200px; }
</style>
<input id="name" style="top: 20px">
<button id="send" style="top: 80px" onclick="
  document.getElementById('said').textContent =
      document.getElementById('name').value">Send</button>
<p id="said" style="top: 140px"></p>)";

std::unique_ptr<net::test_server::HttpResponse> ServeForm(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/form") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(kFormPage);
  return response;
}

class AgentEngineBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    // The engine refuses to act on a bare IP address.
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    // It navigates over http but acts only on https pages.
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(
        base::BindRepeating(&ServeForm));
    ASSERT_TRUE(embedded_https_test_server().Start());
  }

  void TearDownOnMainThread() override {
    // Each task's engine holds the profile, which must outlive it.
    if (engine()) {
      engine()->ResetForTesting();
    }
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  actor::ActorKeyedService* engine() {
    return actor::ActorKeyedService::Get(browser()->GetProfile());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  // A task of uw's own that may act in `tab`.
  actor::TaskId StartTask(tabs::TabInterface& tab) {
    const actor::TaskId task = engine()->CreateTask(
        actor::TaskSourceInfo(actor::TaskSourceInfo::Client::kExperimentalActor,
                              std::nullopt),
        actor::GetNullEnterprisePolicyChecker());
    base::test::TestFuture<actor::mojom::ActionResultPtr> added;
    engine()->GetTask(task)->AddTab(
        tab.GetHandle(), /*stop_task_on_detach=*/true, added.GetCallback());
    EXPECT_EQ(added.Get()->code, ActionResultCode::kOk) << added.Get()->message;
    return task;
  }

  // Runs one action and returns how it ended.
  ActionResultCode Act(actor::TaskId task,
                       std::unique_ptr<actor::ToolRequest> action) {
    std::vector<std::unique_ptr<actor::ToolRequest>> actions;
    actions.push_back(std::move(action));
    base::test::TestFuture<std::vector<actor::ActionResultWithLatencyInfo>,
                           actor::TabObservationStrategy>
        done;
    engine()->PerformActions(task, std::move(actions),
                             actor::ActorTaskMetadata(), done.GetCallback());
    const auto& results = done.Get<0>();
    if (results.size() != 1u) {
      ADD_FAILURE() << results.size() << " results for one action";
      return ActionResultCode::kOk;
    }
    EXPECT_EQ(results[0].result->code, ActionResultCode::kOk)
        << results[0].result->message;
    return results[0].result->code;
  }

  // A background tab showing the form, with a task that may act in it.
  tabs::TabInterface* OpenFormInTheBackground(actor::TaskId* task) {
    std::set<tabs::TabInterface*> before;
    for (int i = 0; i < tabs()->count(); ++i) {
      before.insert(tabs()->GetTabAtIndex(i));
    }
    chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                     /*foreground=*/false);
    tabs::TabInterface* tab = nullptr;
    for (int i = 0; i < tabs()->count(); ++i) {
      if (!before.contains(tabs()->GetTabAtIndex(i))) {
        tab = tabs()->GetTabAtIndex(i);
      }
    }
    CHECK(tab);
    *task = StartTask(*tab);
    Act(*task, std::make_unique<actor::NavigateToolRequest>(
                   tab->GetHandle(),
                   embedded_https_test_server().GetURL("shop.test", "/form")));
    return tab;
  }

  static gfx::Point CenterOf(content::WebContents* contents,
                             std::string_view id) {
    const std::string script = "(() => { const r = document.getElementById('" +
                               std::string(id) +
                               "').getBoundingClientRect();"
                               " return [r.x + r.width / 2, r.y + r.height / "
                               "2].map(Math.round).join(','); })()";
    const std::string center =
        content::EvalJs(contents, script).ExtractString();
    const size_t comma = center.find(',');
    return gfx::Point(std::stoi(center.substr(0, comma)),
                      std::stoi(center.substr(comma + 1)));
  }
};

IN_PROC_BROWSER_TEST_F(AgentEngineBrowserTest, TheEngineIsOnInUw) {
  ASSERT_TRUE(engine());
}

IN_PROC_BROWSER_TEST_F(AgentEngineBrowserTest,
                       FillsAFormInABackgroundTabWithoutSelectingIt) {
  ASSERT_TRUE(engine());
  const int selected = tabs()->active_index();
  actor::TaskId task;
  tabs::TabInterface* tab = OpenFormInTheBackground(&task);
  content::WebContents* page = tab->GetContents();
  ASSERT_EQ(page->GetLastCommittedURL().path(), "/form");

  Act(task,
      std::make_unique<actor::ClickToolRequest>(
          tab->GetHandle(), CenterOf(page, "name"),
          actor::mojom::ClickType::kLeft, actor::mojom::ClickCount::kSingle));
  Act(task,
      std::make_unique<actor::TypeToolRequest>(
          tab->GetHandle(), CenterOf(page, "name"), "hello",
          /*follow_by_enter=*/false, actor::TypeToolRequest::Mode::kReplace));
  Act(task,
      std::make_unique<actor::ClickToolRequest>(
          tab->GetHandle(), CenterOf(page, "send"),
          actor::mojom::ClickType::kLeft, actor::mojom::ClickCount::kSingle));

  EXPECT_EQ(
      content::EvalJs(page, "document.getElementById('said').textContent"),
      "hello");
  EXPECT_EQ(tabs()->active_index(), selected);
}

IN_PROC_BROWSER_TEST_F(AgentEngineBrowserTest,
                       ReadsABackgroundTabWithAScreenshot) {
  ASSERT_TRUE(engine());
  actor::TaskId task;
  tabs::TabInterface* tab = OpenFormInTheBackground(&task);

  base::test::TestFuture<actor::ActorKeyedService::TabObservationResult> seen;
  engine()->RequestTabObservation(*tab, task, std::nullopt, seen.GetCallback());
  const actor::ActorKeyedService::TabObservationResult& result = seen.Get();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_TRUE(result.value());
  const auto& observation = *result.value();
  ASSERT_TRUE(observation.annotated_page_content_result.has_value())
      << observation.annotated_page_content_result.error();
  EXPECT_TRUE(observation.annotated_page_content_result->proto.has_root_node());
  ASSERT_TRUE(observation.screenshot_result.has_value())
      << observation.screenshot_result.error();
  EXPECT_FALSE(observation.screenshot_result->screenshot_data.empty());
}

}  // namespace
}  // namespace uw
