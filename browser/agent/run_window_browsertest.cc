// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/run_window.h"

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

std::unique_ptr<net::test_server::HttpResponse> Serve(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  if (request.relative_url == "/desk") {
    response->set_content(
        "<!doctype html><title>Jarvis desk</title><p>Price: $549</p>"
        "<a href=\"/reviews\">Reviews</a>");
  } else if (request.relative_url == "/reviews") {
    response->set_content("<!doctype html><title>Reviews</title>");
  } else {
    return nullptr;
  }
  return response;
}

class RunWindowBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(
        base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_https_test_server().Start());
    profile_ = browser()->GetProfile();
  }

  void TearDownOnMainThread() override {
    tools_.reset();
    actor::ActorKeyedService::Get(profile_)->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  GURL Url(std::string_view path) {
    return embedded_https_test_server().GetURL("shop.test", path);
  }

  // Opens a run's tab and gives the agent its tools there.
  tabs::TabInterface* StartRun() {
    tabs::TabInterface* run_tab = OpenRunTab(profile_);
    if (run_tab) {
      tools_ = std::make_unique<AgentTools>(*run_tab);
    }
    return run_tab;
  }

  base::DictValue Call(std::string_view tool, base::DictValue args) {
    base::test::TestFuture<base::DictValue> done;
    tools_->Call(tool, args, done.GetCallback());
    return done.Take();
  }

  int Open(std::string_view path) {
    const base::DictValue opened =
        Call("open", base::DictValue().Set("url", Url(path).spec()));
    EXPECT_FALSE(opened.FindString("error")) << *opened.FindString("error");
    return opened.FindInt("tab").value_or(0);
  }

  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<AgentTools> tools_;
};

IN_PROC_BROWSER_TEST_F(RunWindowBrowserTest,
                       ARunReadsPagesWithNoWindowOpenAndNothingShows) {
  // Chromium unloads a profile when its last window closes. Scheduled chats
  // keep theirs loaded, as these do here; the run window then keeps it too.
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile_, ProfileKeepAliveOrigin::kBrowserWindow);
  CloseBrowserSynchronously(browser());

  tabs::TabInterface* run_tab = StartRun();
  ASSERT_TRUE(run_tab);
  BrowserWindowInterface* window = run_tab->GetBrowserWindowInterface();
  EXPECT_TRUE(IsRunWindow(window));
  const int tab = Open("/desk");
  const base::DictValue page = Call("read", base::DictValue().Set("tab", tab));
  ASSERT_FALSE(page.FindString("error")) << *page.FindString("error");
  EXPECT_EQ(*page.FindString("title"), "Jarvis desk");

  EXPECT_EQ(tools_->OpenTabs().at(tab)->GetBrowserWindowInterface(), window);
  EXPECT_FALSE(window->GetWindow()->IsVisible());
  EXPECT_FALSE(TabOrganizationController::From(window));
  EXPECT_FALSE(GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser());
  EXPECT_FALSE(
      ProfileBrowserCollection::GetForProfile(profile_)->FindTabbedBrowser());

  // A window the user opens now is theirs, with uw's features, and the one
  // they used last.
  Browser* next = CreateBrowser(profile_);
  SetBrowser(next);
  EXPECT_TRUE(TabOrganizationController::From(next));
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser(),
            next);
  EXPECT_FALSE(IsRunWindow(next));
}

IN_PROC_BROWSER_TEST_F(RunWindowBrowserTest, RunsShareOneRunWindow) {
  tabs::TabInterface* first = OpenRunTab(profile_);
  tabs::TabInterface* second = OpenRunTab(profile_);
  ASSERT_TRUE(first && second);
  EXPECT_NE(first, second);
  EXPECT_EQ(first->GetBrowserWindowInterface(),
            second->GetBrowserWindowInterface());
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser(),
            browser());
}

IN_PROC_BROWSER_TEST_F(RunWindowBrowserTest,
                       ClosingARunsTabsClosesTheWindowAndKeepsNoHistory) {
  tabs::TabInterface* run_tab = StartRun();
  ASSERT_TRUE(run_tab);
  BrowserWindowInterface* window = run_tab->GetBrowserWindowInterface();
  Open("/desk");
  Open("/reviews");
  ASSERT_EQ(window->GetTabStripModel()->count(), 3);
  tools_.reset();

  CloseRunTabs(*run_tab);

  ASSERT_TRUE(base::test::RunUntil([&] { return !IsRunWindow(window); }));
  sessions::TabRestoreService* history =
      TabRestoreServiceFactory::GetForProfile(profile_);
  EXPECT_TRUE(history->entries().empty());
}

IN_PROC_BROWSER_TEST_F(RunWindowBrowserTest,
                       PRE_TheRunWindowIsNotRestoredAfterARestart) {
  SessionStartupPref::SetStartupPref(
      profile_, SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(), Url("/reviews")));
  ASSERT_TRUE(StartRun());
  Open("/desk");
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(RunWindowBrowserTest,
                       TheRunWindowIsNotRestoredAfterARestart) {
  bool restored_reviews = false;
  bool restored_desk = false;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    EXPECT_FALSE(IsRunWindow(window));
    TabStripModel* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      const std::string path(tabs->GetWebContentsAt(i)->GetVisibleURL().path());
      restored_reviews |= path == "/reviews";
      restored_desk |= path == "/desk";
    }
  }
  EXPECT_TRUE(restored_reviews);
  EXPECT_FALSE(restored_desk);
}

}  // namespace
}  // namespace uw
