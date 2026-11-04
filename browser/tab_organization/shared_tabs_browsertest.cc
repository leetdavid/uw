// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
#include "chrome/browser/actor/enterprise_policy_checker.h"
#include "chrome/browser/autocomplete/tab_matcher_desktop.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_active_state_manager/browser_active_state_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/tab_group_sync_service_initialized_observer.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/webui/metrics_reporter/mock_metrics_reporter.h"
#include "chrome/browser/ui/webui/tab_search/tab_search.mojom.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_page_handler.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_ui.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/actor.mojom.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/actor/core/task_source_info.h"
#include "components/actor/public/mojom/actor_types.mojom.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver.h"
#include "components/performance_manager/performance_manager_tab_helper.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/agent/run_window.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/ui/views/pins/inactive_pin_overlay.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kMail[] = "data:text/html,<title>Mail</title>";
constexpr char kIssues[] = "data:text/html,<title>Issues</title>";
constexpr char kIssue[] = "data:text/html,<title>Issue 412</title>";
constexpr char kNotes[] = "data:text/html,<title>Notes</title><input id=f>";
constexpr char kTypeIntoNotes[] =
    "document.getElementById('f').value = 'half-typed'";
constexpr char kNotesInput[] = "document.getElementById('f').value";

TabOrganizationController* ControllerOf(Browser* window) {
  return TabOrganizationController::From(window);
}

// Each regular tab of `window`, in order: its address, its task's title, or
// "-" for the Scratchpad, with "folded" when the task is, and its depth.
std::vector<std::string> Layout(Browser* window) {
  TabStripModel* tabs = window->tab_strip_model();
  std::vector<std::string> rows;
  for (int i = tabs->IndexOfFirstNonPinnedTab(); i < tabs->count(); ++i) {
    tabs::TabInterface* tab = tabs->GetTabAtIndex(i);
    std::string task = "-";
    if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
      const tab_groups::TabGroupVisualData* visuals =
          tabs->group_model()->GetTabGroup(*group)->visual_data();
      task = base::UTF16ToUTF8(visuals->title()) +
             (visuals->is_collapsed() ? " folded" : "");
    }
    rows.push_back(base::StringPrintf(
        "%s %s %d", tab->GetContents()->GetLastCommittedURL().spec().c_str(),
        task.c_str(), ControllerOf(window)->DepthFor(tab)));
  }
  return rows;
}

// `window`'s tab for the page at `url`.
tabs::TabInterface* TabAt(Browser* window, const char* url) {
  TabStripModel* tabs = window->tab_strip_model();
  for (int i = 0; i < tabs->count(); ++i) {
    if (tabs->GetWebContentsAt(i)->GetLastCommittedURL() == GURL(url)) {
      return tabs->GetTabAtIndex(i);
    }
  }
  return nullptr;
}

bool InStep(Browser* one, Browser* two) {
  return Layout(one) == Layout(two);
}

void SelectByClick(Browser* window, tabs::TabInterface* tab) {
  window->tab_strip_model()->ActivateTabAt(
      window->tab_strip_model()->GetIndexOfTab(tab),
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kMouse));
}

// Brings `window` to the front, as a click on it would. A headless window
// stays active once shown, so it is made inactive first.
void Focus(Browser* window) {
  BrowserActiveStateManager::From(window)->DidBecomeInactive();
  window->GetWindow()->Activate();
}

views::View* FindRow(views::View* view, const tabs::TabInterface* tab) {
  if (auto* row = views::AsViewClass<TabView>(view);
      row && row->GetTabInterface() == tab) {
    return row;
  }
  for (views::View* child : view->children()) {
    if (views::View* row = FindRow(child, tab)) {
      return row;
    }
  }
  return nullptr;
}

// The row in `window`'s tab strip that shows `tab`.
views::View* RowOf(Browser* window, const tabs::TabInterface* tab) {
  return FindRow(BrowserView::GetBrowserViewForBrowser(window), tab);
}

bool IsOpen(BrowserWindowInterface* window) {
  return std::ranges::contains(GetAllBrowserWindowInterfaces(), window);
}

// The window opened since `known` were, or null.
Browser* NewWindow(const std::vector<BrowserWindowInterface*>& known) {
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (!std::ranges::contains(known, window)) {
      return window->GetBrowserForMigrationOnly();
    }
  }
  return nullptr;
}

// How many of `window`'s tabs show the new tab page.
int BlankTabs(Browser* window) {
  int blank = 0;
  for (int i = 0; i < window->tab_strip_model()->count(); ++i) {
    const GURL& url =
        window->tab_strip_model()->GetWebContentsAt(i)->GetVisibleURL();
    if (url.SchemeIs("chrome") &&
        (url.host() == "newtab" || url.host() == "new-tab-page")) {
      ++blank;
    }
  }
  return blank;
}

// How many of `window`'s tabs show `url`.
int Count(Browser* window, const char* url) {
  int count = 0;
  for (int i = 0; i < window->tab_strip_model()->count(); ++i) {
    if (window->tab_strip_model()->GetWebContentsAt(i)->GetLastCommittedURL() ==
        GURL(url)) {
      ++count;
    }
  }
  return count;
}

// How many tabs of `windows` hold the live page of `url`, not a copy.
int LivePages(const std::vector<Browser*>& windows, const char* url) {
  int live = 0;
  for (Browser* window : windows) {
    for (int i = 0; i < window->tab_strip_model()->count(); ++i) {
      tabs::TabInterface* tab = window->tab_strip_model()->GetTabAtIndex(i);
      if (tab->GetContents()->GetLastCommittedURL() == GURL(url) &&
          !TabOrganizationController::IsCopy(tab)) {
        ++live;
      }
    }
  }
  return live;
}

// Whether `page`'s row shows the page's own icon, not the default one.
bool ShowsIcon(content::WebContents* page) {
  favicon::FaviconDriver* icons =
      favicon::ContentFaviconDriver::FromWebContents(page);
  return icons && icons->FaviconIsValid() && !icons->GetFavicon().IsEmpty();
}

// `window`'s tab for the page with an icon, whatever port it was served on.
tabs::TabInterface* IconPageIn(BrowserWindowInterface* window) {
  TabStripModel* tabs = window->GetTabStripModel();
  for (int i = 0; i < tabs->count(); ++i) {
    if (tabs->GetWebContentsAt(i)->GetLastCommittedURL().path() ==
        "/favicon/page_with_favicon.html") {
      return tabs->GetTabAtIndex(i);
    }
  }
  return nullptr;
}

// Whether `window`'s row for `tab` shows the title and icon of `tab`'s page,
// and the icon is the page's own.
bool RowShowsItsPage(Browser* window, tabs::TabInterface* tab) {
  const TabView* row = views::AsViewClass<TabView>(RowOf(window, tab));
  TabUIHelper* shown = TabUIHelper::From(tab);
  return row && ShowsIcon(tab->GetContents()) &&
         row->data().title == shown->GetTitle() &&
         row->data().favicon == shown->GetFavicon();
}

// `profile`'s windows that show.
std::vector<Browser*> ShownWindows(Profile* profile) {
  std::vector<Browser*> shown;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (window->GetProfile() == profile && window->GetWindow()->IsVisible()) {
      shown.push_back(window->GetBrowserForMigrationOnly());
    }
  }
  return shown;
}

// The window of `windows` whose selected tab shows `url`, or null.
Browser* Selecting(const std::vector<Browser*>& windows, const char* url) {
  for (Browser* window : windows) {
    content::WebContents* selected =
        window->tab_strip_model()->GetActiveWebContents();
    if (selected && selected->GetLastCommittedURL() == GURL(url)) {
      return window;
    }
  }
  return nullptr;
}

// The windows' tabs, with each one's page id and whether it holds a copy, for
// a failure message.
std::string Layouts(const std::vector<Browser*>& windows) {
  std::string text;
  for (Browser* window : windows) {
    text += "window:\n";
    TabStripModel* tabs = window->tab_strip_model();
    for (int i = 0; i < tabs->count(); ++i) {
      tabs::TabInterface* tab = tabs->GetTabAtIndex(i);
      const std::optional<PageId> id = ControllerOf(window)->IdFor(tab);
      text += base::StringPrintf(
          "  %s %s%s\n",
          tab->GetContents()->GetLastCommittedURL().spec().c_str(),
          id ? id->value().c_str() : "-",
          TabOrganizationController::IsCopy(tab) ? " copy" : "");
    }
  }
  return text;
}

// The items after New Task or New Window in the submenu that `command` opens
// in `menu`, or none without that submenu.
std::vector<std::u16string> ItemsOf(ui::SimpleMenuModel& menu, int command) {
  std::vector<std::u16string> items;
  const std::optional<size_t> row = menu.GetIndexOfCommandId(command);
  if (!row) {
    return items;
  }
  ui::MenuModel* submenu = menu.GetSubmenuModelAt(*row);
  for (size_t i = 1; i < submenu->GetItemCount(); ++i) {
    if (submenu->GetTypeAt(i) == ui::MenuModel::TYPE_COMMAND) {
      items.push_back(submenu->GetLabelAt(i));
    }
  }
  return items;
}

// A tab search page that only listens.
class QuietTabSearchPage : public tab_search::mojom::Page {
 public:
  mojo::PendingRemote<tab_search::mojom::Page> Remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  // tab_search::mojom::Page:
  void HostWindowChanged() override {}
  void TabsChanged(tab_search::mojom::ProfileDataPtr) override {}
  void TabUpdated(tab_search::mojom::TabUpdateInfoPtr) override {}
  void TabsRemoved(tab_search::mojom::TabsRemovedInfoPtr) override {}
  void TabUnsplit() override {}

 private:
  mojo::Receiver<tab_search::mojom::Page> receiver_{this};
};

class SharedTabsBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    // A page that counts in its title, as a timer does.
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/timer") {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(
              "<title>0</title><script>let n = 0;"
              "setInterval(() => document.title = ++n, 1000)</script>");
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  // Waits until `page` has loaded and gone quiet, from when Memory Saver
  // counts its title changes out of sight as work, and for one more change.
  static void WaitUntilAtWork(content::WebContents* page) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      base::WeakPtr<performance_manager::PageNode> node = performance_manager::
          PerformanceManager::GetPrimaryPageNodeForWebContents(page);
      return node &&
             node->GetLoadingState() ==
                 performance_manager::PageNode::LoadingState::kLoadedIdle;
    }));
    const std::u16string title = page->GetTitle();
    ASSERT_TRUE(
        base::test::RunUntil([&] { return page->GetTitle() != title; }));
  }

  // Chromium's saved tab groups miss a task named before they load, and at
  // the next launch put their copy, without the name, back. A user is never
  // that quick, so tests that restart wait.
  void WaitForSavedGroups() {
    tab_groups::TabGroupSyncServiceInitializedObserver(
        tab_groups::TabGroupSyncServiceFactory::GetForProfile(
            browser()->GetProfile()))
        .Wait();
  }

  tabs::TabInterface* Open(Browser* window,
                           const char* url,
                           bool select = false) {
    content::WebContents* page =
        chrome::AddAndReturnTabAt(window, GURL(url), -1, select);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    base::RunLoop().RunUntilIdle();
    return window->tab_strip_model()->GetTabForWebContents(page);
  }
};

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, WindowsOnAWorkspaceShowOneTree) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Layout(second).size() == 4u && InStep(first, second); }));

  // A tab opened in either window shows in the other.
  Open(second, kIssue);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Layout(first).size() == 5u && InStep(first, second); }));

  // A task made in one window, renamed in the other, and folded in the first.
  const std::optional<PageId> mail =
      ControllerOf(first)->IdFor(TabAt(first, kMail));
  const std::optional<PageId> issues =
      ControllerOf(first)->IdFor(TabAt(first, kIssues));
  ASSERT_TRUE(mail && issues);
  ASSERT_TRUE(ControllerOf(first)->MoveToNewTask({*mail, *issues}, u"Trip"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kMail)->GetGroup();
  }));
  EXPECT_NE(Layout(second)[0].find(" Trip "), std::string::npos);

  TabStripModel* second_tabs = second->tab_strip_model();
  const tab_groups::TabGroupId copy = *TabAt(second, kMail)->GetGroup();
  second_tabs->ChangeTabGroupVisuals(
      copy, tab_groups::TabGroupVisualData(
                u"Lisbon", tab_groups::TabGroupColorId::kGreen));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_NE(Layout(first)[0].find(" Lisbon "), std::string::npos);

  const tab_groups::TabGroupId task = *TabAt(first, kMail)->GetGroup();
  first->tab_strip_model()->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(u"Lisbon",
                                           tab_groups::TabGroupColorId::kGreen,
                                           /*is_collapsed=*/true));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_NE(Layout(second)[0].find(" Lisbon folded "), std::string::npos);

  // Nesting in one window indents the other. A tab opened in the background
  // goes right under the selected tab, so Issues is above Mail.
  ASSERT_TRUE(ControllerOf(first)->Nest(*mail));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) &&
           ControllerOf(second)->DepthFor(TabAt(second, kMail)) == 1;
  }));

  // Closing a copy closes the page in every window, and its child moves up.
  second_tabs->CloseWebContentsAt(
      second_tabs->GetIndexOfTab(TabAt(second, kIssues)),
      TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabAt(first, kIssues) && InStep(first, second); }));
  EXPECT_EQ(ControllerOf(first)->DepthFor(TabAt(first, kMail)), 0);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AnotherWorkspaceShowsNoneOfIt) {
  Browser* first = browser();
  Open(first, kMail);
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal =
      list.Add("Personal", std::string(list.UnusedIcon()),
               first->GetProfile()->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->WithPartner(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* opened) { *partner = opened; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return partner && partner->GetTabStripModel()->count() == 1; }));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(partner->GetBrowserForMigrationOnly(), kMail));
  EXPECT_EQ(Layout(first).size(), 2u);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, SelectingACopyBringsItsLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::ExecJs(notes->GetContents(), kTypeIntoNotes));
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  tabs::TabInterface* copy = TabAt(second, kNotes);
  ASSERT_TRUE(TabOrganizationController::IsCopy(copy));

  SelectByClick(second, copy);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabOrganizationController::IsCopy(copy); }));
  EXPECT_TRUE(TabOrganizationController::IsCopy(notes));
  EXPECT_EQ("half-typed", content::EvalJs(copy->GetContents(), kNotesInput));
  // The first window still has it selected, so it shows the hazed view.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return InactivePinOverlay::From(first)->GetShownView(); }));

  ControllerOf(first)->BringLivePage(notes);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabOrganizationController::IsCopy(notes); }));
  EXPECT_EQ("half-typed", content::EvalJs(notes->GetContents(), kNotesInput));
  EXPECT_FALSE(InactivePinOverlay::From(first)->GetShownView());
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       FocusingTheWindowBringsItsLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::ExecJs(notes->GetContents(), kTypeIntoNotes));
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  SelectByClick(second, TabAt(second, kNotes));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return InactivePinOverlay::From(first)->GetShownView(); }));

  Focus(first);

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabOrganizationController::IsCopy(notes); }));
  EXPECT_EQ("half-typed", content::EvalJs(notes->GetContents(), kNotesInput));
  EXPECT_FALSE(InactivePinOverlay::From(first)->GetShownView());
  EXPECT_TRUE(InactivePinOverlay::From(second)->GetShownView());
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ACopyFollowsItsLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  ASSERT_TRUE(
      content::ExecJs(notes->GetContents(), "document.title = 'Plans'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return TabAt(second, kNotes)->GetContents()->GetTitle() == u"Plans";
  }));
}

// A tab opened in one window shows its page's title in the other window's
// row, though the copy there was made before the page had loaded.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ACopysRowShowsItsPagesTitle) {
  Browser* first = browser();
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));

  Open(first, kMail, /*select=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    const tabs::TabInterface* copy = TabAt(second, kMail);
    return copy && copy->GetContents()->GetTitle() == u"Mail";
  }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const TabView* row =
        views::AsViewClass<TabView>(RowOf(second, TabAt(second, kMail)));
    return row && row->data().title == u"Mail";
  }));
}

// A copy's row shows its page's icon too, once the page has one.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ACopysRowShowsItsPagesIcon) {
  Browser* first = browser();
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));

  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::NavigateToURL(
      notes->GetContents(),
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html")));
  EXPECT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* copy = IconPageIn(second);
    return copy && TabOrganizationController::IsCopy(copy) &&
           RowShowsItsPage(second, copy);
  }));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ReloadInACopyRunsInTheLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  tabs::TabInterface* copy = TabAt(second, kNotes);
  // Selected without a click, while the first window shows it, so it stays a
  // copy.
  second->tab_strip_model()->ActivateTabAt(
      second->tab_strip_model()->GetIndexOfTab(copy));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(TabOrganizationController::IsCopy(copy));

  chrome::Reload(second, WindowOpenDisposition::CURRENT_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(copy) &&
           TabOrganizationController::IsCopy(notes);
  }));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, PrivateWindowsKeepTheirOwnTabs) {
  Browser* first = browser();
  Open(first, kMail);
  Browser* private_window = CreateIncognitoBrowser(first->GetProfile());
  Open(private_window, kIssues);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(private_window, kMail));
  EXPECT_FALSE(TabAt(first, kIssues));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ClosingOneOfTwoWindowsClosesNoTab) {
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::ExecJs(notes->GetContents(), kTypeIntoNotes));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));

  CloseBrowserSynchronously(first);
  tabs::TabInterface* kept = TabAt(second, kNotes);
  ASSERT_TRUE(kept);
  // The second window took the live page, half-typed input and all.
  EXPECT_FALSE(TabOrganizationController::IsCopy(kept));
  EXPECT_EQ("half-typed", content::EvalJs(kept->GetContents(), kNotesInput));
  // Cmd+Shift+T has no window to bring back.
  for (const auto& entry :
       TabRestoreServiceFactory::GetForProfile(profile)->entries()) {
    EXPECT_NE(entry->type, sessions::tab_restore::WINDOW);
  }
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       TheLastWindowKeepsItsTabsUntilAnotherShowsThem) {
  Profile* profile = browser()->GetProfile();
  // A private window, so the app keeps a window of its own.
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  Open(first, kMail);
  content::WebContents* timer = chrome::AddAndReturnTabAt(
      first, embedded_test_server()->GetURL("/timer"), -1, false);
  WaitUntilAtWork(timer);
  const GURL timer_url = timer->GetLastCommittedURL();

  // Closing it hides it. The page doing nothing unloads, and the timer
  // keeps counting.
  first->GetWindow()->Close();
  WorkspaceWindow* kept = WorkspaceWindow::From(first);
  ASSERT_TRUE(base::test::RunUntil([&] { return kept->keeper(); }));
  EXPECT_TRUE(IsOpen(first));
  EXPECT_FALSE(first->GetWindow()->IsVisible());
  EXPECT_TRUE(TabOrganizationController::IsCopy(TabAt(first, kMail)));
  EXPECT_FALSE(TabOrganizationController::IsCopy(
      first->tab_strip_model()->GetTabForWebContents(timer)));

  // A new window on the workspace shows the tabs, takes the timer live, and
  // the kept window closes.
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !IsOpen(first) && TabAt(second, kMail); }));
  tabs::TabInterface* counting = nullptr;
  for (int i = 0; i < second->tab_strip_model()->count(); ++i) {
    if (second->tab_strip_model()->GetWebContentsAt(i)->GetLastCommittedURL() ==
        timer_url) {
      counting = second->tab_strip_model()->GetTabAtIndex(i);
    }
  }
  ASSERT_TRUE(counting);
  EXPECT_FALSE(TabOrganizationController::IsCopy(counting));
  // The unloaded page loads again once selected.
  tabs::TabInterface* mail = TabAt(second, kMail);
  SelectByClick(second, mail);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(mail) &&
           mail->GetContents()->GetTitle() == u"Mail";
  }));
}

// A page no window holds, as one unloaded while the last window was kept,
// loads where it is selected, and the hazed view over it goes.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       APageNoWindowHoldsLoadsWithoutTheHazedView) {
  Profile* profile = browser()->GetProfile();
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  Open(first, kMail);
  first->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return WorkspaceWindow::From(first)->keeper(); }));
  ASSERT_TRUE(TabOrganizationController::IsCopy(TabAt(first, kMail)));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !IsOpen(first) && TabAt(second, kMail); }));
  // A tab opened here closes the window's unused blank tab now, so selecting
  // Mail is the last change to the tab strip.
  Open(second, kIssues, /*select=*/true);
  tabs::TabInterface* mail = TabAt(second, kMail);
  ASSERT_TRUE(TabOrganizationController::IsCopy(mail));

  SelectByClick(second, mail);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(mail) &&
           mail->GetContents()->GetTitle() == u"Mail";
  }));
  EXPECT_FALSE(InactivePinOverlay::From(second)->GetShownView());
}

// The last window keeps live the pages still at work, as Memory Saver would:
// one that plays sound, one with unsaved input, and one the agent works in.
// A page doing nothing unloads.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       TheLastWindowKeepsPagesAtWorkLive) {
  Profile* profile = browser()->GetProfile();
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  tabs::TabInterface* mail = Open(first, kMail);
  tabs::TabInterface* playing = Open(first, kIssues);
  tabs::TabInterface* agent = Open(first, kIssue);
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);

  // Typed into, as a user types.
  ASSERT_TRUE(content::ExecJs(notes->GetContents(),
                              "document.getElementById('f').focus()"));
  content::SimulateKeyPress(notes->GetContents(),
                            ui::DomKey::FromCharacter('a'), ui::DomCode::US_A,
                            ui::VKEY_A, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil([&] {
    base::WeakPtr<performance_manager::PageNode> node =
        performance_manager::PerformanceManager::
            GetPrimaryPageNodeForWebContents(notes->GetContents());
    return node && (node->HadUserEdits() || node->HadFormInteraction());
  }));
  // Sound, as Chromium hears it. Tests play none.
  performance_manager::PerformanceManagerTabHelper::FromWebContents(
      playing->GetContents())
      ->OnAudioStateChanged(true);
  actor::ActorKeyedService* agents = actor::ActorKeyedService::Get(profile);
  const actor::TaskId task = agents->CreateTask(
      actor::TaskSourceInfo(actor::TaskSourceInfo::Client::kExperimentalActor,
                            std::nullopt),
      actor::GetNullEnterprisePolicyChecker());
  base::test::TestFuture<actor::mojom::ActionResultPtr> added;
  agents->GetTask(task)->AddTab(agent->GetHandle(),
                                /*stop_task_on_detach=*/true,
                                added.GetCallback());
  ASSERT_EQ(added.Get()->code, actor::mojom::ActionResultCode::kOk);

  first->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return WorkspaceWindow::From(first)->keeper(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabOrganizationController::IsCopy(mail); }));
  EXPECT_FALSE(TabOrganizationController::IsCopy(playing));
  EXPECT_FALSE(TabOrganizationController::IsCopy(notes));
  EXPECT_FALSE(TabOrganizationController::IsCopy(agent));
  agents->ResetForTesting();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       TheLastTabClosesEveryWindowOnTheWorkspace) {
  Profile* profile = browser()->GetProfile();
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  // A pin keeps each window's tab strip from emptying.
  tabs::TabInterface* pin = Open(first, kMail);
  first->tab_strip_model()->SetTabPinned(
      first->tab_strip_model()->GetIndexOfTab(pin), true);
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kMail) &&
           second->tab_strip_model()->IndexOfFirstNonPinnedTab() == 1;
  }));

  // Closing the last tab closes the windows, so they close in one go.
  TabStripModel* tabs = first->tab_strip_model();
  const int regular = tabs->count() - tabs->IndexOfFirstNonPinnedTab();
  for (int i = 0; i < regular; ++i) {
    tabs->CloseWebContentsAt(tabs->count() - 1,
                             TabCloseTypes::CLOSE_USER_GESTURE);
  }
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !IsOpen(first) && !IsOpen(second); }));
}

// Moving a tab to a window of its own, as dragging it out does, opens a
// window on its workspace that shows it, and the tab keeps its place. Moving
// it back closes that window.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ATabMovedOutKeepsItsPlaceAndItsWindowClosesAfter) {
  Browser* first = browser();
  Open(first, kMail);
  tabs::TabInterface* issues = Open(first, kIssues);
  Open(first, kIssue);
  const std::vector<std::string> before = Layout(first);
  const PageId id = ControllerOf(first)->IdFor(issues).value();
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();

  chrome::MoveTabsToNewWindow(
      first, {first->tab_strip_model()->GetIndexOfTab(issues)});
  Browser* own = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    own = NewWindow(known);
    return own && ControllerOf(own)->TabFor(id) && InStep(first, own);
  }));
  EXPECT_EQ(Layout(first), before);
  EXPECT_EQ(own->tab_strip_model()->GetActiveTab(),
            ControllerOf(own)->TabFor(id));
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(own, kIssues)));
  EXPECT_TRUE(TabOrganizationController::IsCopy(TabAt(first, kIssues)));

  chrome::MoveTabsToExistingWindow(
      own, first, {own->tab_strip_model()->GetIndexOfTab(TabAt(own, kIssues))});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !IsOpen(own) && TabAt(first, kIssues) &&
           !TabOrganizationController::IsCopy(TabAt(first, kIssues));
  }));
  EXPECT_EQ(std::ranges::count_if(Layout(first),
                                  [](const std::string& row) {
                                    return row.starts_with(
                                        std::string(kIssues) + " ");
                                  }),
            1);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AWindowUsedForAnotherTabStays) {
  Browser* first = browser();
  Open(first, kMail);
  tabs::TabInterface* issues = Open(first, kIssues);
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();
  chrome::MoveTabsToNewWindow(
      first, {first->tab_strip_model()->GetIndexOfTab(issues)});
  Browser* own = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    own = NewWindow(known);
    return own && TabAt(own, kMail) && InStep(first, own);
  }));

  SelectByClick(own, TabAt(own, kMail));
  chrome::MoveTabsToExistingWindow(
      own, first, {own->tab_strip_model()->GetIndexOfTab(TabAt(own, kIssues))});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(TabAt(first, kIssues));
  }));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(IsOpen(own));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ATabMovedToAnotherWorkspaceLeavesThisOne) {
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  tabs::TabInterface* mail = Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabAt(second, kMail) && InStep(first, second); }));
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal = list.Add(
      "Personal", std::string(list.UnusedIcon()), profile->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->WithPartner(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* opened) { *partner = opened; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return partner && partner->GetTabStripModel()->count() == 1; }));

  chrome::MoveTabsToExistingWindow(
      first, partner, {first->tab_strip_model()->GetIndexOfTab(mail)});
  Browser* other = partner->GetBrowserForMigrationOnly();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return TabAt(other, kMail) && !TabAt(first, kMail) && !TabAt(second, kMail);
  }));
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(other, kMail)));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       CmdNOpensTheCommandBarOverABlankTabThatGoesUnused) {
  Browser* first = browser();
  Open(first, kMail);
  std::vector<BrowserWindowInterface*> known = GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  Browser* fresh = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    fresh = NewWindow(known);
    return fresh && TabAt(fresh, kMail) && BlankTabs(first) == 1 &&
           InStep(first, fresh);
  }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CommandBar::From(fresh)->IsShowing(); }));

  // Closed unused, the window takes its blank tab along.
  CloseBrowserSynchronously(fresh);
  ASSERT_TRUE(base::test::RunUntil([&] { return BlankTabs(first) == 0; }));

  // So does picking another tab in it.
  known = GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] {
    fresh = NewWindow(known);
    return fresh && TabAt(fresh, kMail) && BlankTabs(first) == 1;
  }));
  SelectByClick(fresh, TabAt(fresh, kMail));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return BlankTabs(first) == 0 && BlankTabs(fresh) == 0; }));
}

// Quitting closes the windows, so a new window's unused blank tab goes with
// them and does not come back after a restart.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       PRE_AnUnusedBlankTabGoesWhenUwQuits) {
  WaitForSavedGroups();
  Browser* first = browser();
  SessionStartupPref::SetStartupPref(
      first->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, GURL(kMail)));
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] {
    Browser* fresh = NewWindow(known);
    return fresh && TabAt(fresh, kMail) && BlankTabs(first) == 1 &&
           InStep(first, fresh);
  }));
  // Lets the session take the windows' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AnUnusedBlankTabGoesWhenUwQuits) {
  Profile* profile = browser()->GetProfile();
  std::vector<Browser*> windows;
  ASSERT_TRUE(base::test::RunUntil([&] {
    windows = ShownWindows(profile);
    return windows.size() == 2u && TabAt(windows[0], kMail) &&
           InStep(windows[0], windows[1]);
  })) << Layouts(ShownWindows(profile));
  for (Browser* window : windows) {
    EXPECT_EQ(BlankTabs(window), 0) << Layouts(windows);
  }
}

// A new window on a workspace with many tabs shows at once and fills in with
// copies after, a slice at a time, while every page keeps its place.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ANewWindowFillsInAfterItShows) {
  WaitForSavedGroups();
  Browser* first = browser();
  constexpr int kPages = 40;
  for (int i = 0; i < kPages; ++i) {
    chrome::AddTabAt(
        first,
        GURL(base::StringPrintf("data:text/html,<title>Page %d</title>", i)),
        -1, false);
  }
  TabStripModel* tabs = first->tab_strip_model();
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetLastCommittedURL().is_empty()) {
        return false;
      }
    }
    return true;
  }));
  // The rows of the pages opened here, leaving out the new window's blank
  // tab.
  const auto pages = [](Browser* window) {
    std::vector<std::string> rows = Layout(window);
    std::erase_if(rows, [](const std::string& row) {
      return !row.starts_with("data:") && !row.starts_with("about:blank");
    });
    return rows;
  };
  const std::vector<std::string> before = pages(first);
  const auto copies = [](Browser* window) {
    int copies = 0;
    for (int i = 0; i < window->tab_strip_model()->count(); ++i) {
      copies += TabOrganizationController::IsCopy(
          window->tab_strip_model()->GetTabAtIndex(i));
    }
    return copies;
  };

  // The window joins as soon as it has its blank tab, takes in a first
  // slice of copies, and gives way to the next task.
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  Browser* fresh = NewWindow(known);
  ASSERT_TRUE(fresh);
  int first_slice = -1;
  base::RunLoop next_task;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        first_slice = copies(fresh);
        next_task.Quit();
      }));
  next_task.Run();
  EXPECT_GT(first_slice, 0);
  EXPECT_LT(first_slice, static_cast<int>(before.size()));
  EXPECT_EQ(pages(first), before);

  ASSERT_TRUE(base::test::RunUntil([&] {
    return copies(fresh) == static_cast<int>(before.size()) &&
           InStep(first, fresh);
  })) << Layouts({first, fresh});
}

// The command bar's list of open tabs, like tab search, has each page once,
// though every window shows it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, TabListsShowEachPageOnce) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  SelectByClick(second, TabAt(second, kIssues));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(TabAt(second, kIssues));
  }));

  std::map<std::string, int> listed;
  for (const auto& tab : TabMatcherDesktop(nullptr, first->GetProfile())
                             .GetOpenTabs(nullptr,
                                          /*exclude_active_tab=*/false)) {
    ++listed[tab.url.spec()];
  }
  EXPECT_EQ(listed[kMail], 1);
  EXPECT_EQ(listed[kIssues], 1);
  int total = 0;
  for (const auto& [url, count] : listed) {
    total += count;
  }
  EXPECT_EQ(total, static_cast<int>(Layout(first).size()));

  // Tab search asks Chromium's handler, which leaves out the same copies.
  std::unique_ptr<content::WebContents> host = content::WebContents::Create(
      content::WebContents::CreateParams(first->GetProfile()));
  content::TestWebUI web_ui;
  web_ui.set_web_contents(host.get());
  webui::SetBrowserWindowInterface(host.get(), first);
  TabSearchUI controller(&web_ui);
  QuietTabSearchPage page;
  testing::NiceMock<MockMetricsReporter> metrics;
  TabSearchPageHandler handler(
      mojo::PendingReceiver<tab_search::mojom::PageHandler>(), page.Remote(),
      &web_ui, &controller, &metrics);
  base::test::TestFuture<tab_search::mojom::ProfileDataPtr> data;
  handler.GetProfileData(data.GetCallback());
  std::map<std::string, int> searched;
  for (const auto& window : data.Get()->windows) {
    for (const auto& tab : window->tabs) {
      ++searched[tab->url.spec()];
    }
  }
  EXPECT_EQ(searched[kMail], 1);
  EXPECT_EQ(searched[kIssues], 1);
}

// Move to Task lists each task once, however many windows show it, and the
// tab moves within its own window.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, MoveToTaskListsEachTaskOnce) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Open(first, kIssue);
  TabOrganizationController* organizer = ControllerOf(first);
  ASSERT_TRUE(organizer->MoveToNewTask({*organizer->IdFor(TabAt(first, kMail))},
                                       u"Trip"));
  ASSERT_TRUE(organizer->MoveToNewTask(
      {*organizer->IdFor(TabAt(first, kIssues))}, u"Bugs"));
  Browser* second = CreateBrowser(first->GetProfile());
  Browser* third = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return InStep(first, second) && InStep(first, third); }));

  TabStripModel* tabs = second->tab_strip_model();
  TabMenuModel menu(nullptr, second->GetFeatures().tab_menu_model_delegate(),
                    tabs, tabs->GetIndexOfTab(TabAt(second, kIssue)));
  EXPECT_THAT(ItemsOf(menu, TabStripModel::CommandAddToExistingGroup),
              testing::UnorderedElementsAre(u"Trip", u"Bugs"));
  TabMenuModel in_trip(
      nullptr, third->GetFeatures().tab_menu_model_delegate(),
      third->tab_strip_model(),
      third->tab_strip_model()->GetIndexOfTab(TabAt(third, kMail)));
  EXPECT_THAT(ItemsOf(in_trip, TabStripModel::CommandAddToExistingGroup),
              testing::ElementsAre(u"Bugs"));

  ui::MenuModel* tasks = menu.GetSubmenuModelAt(
      *menu.GetIndexOfCommandId(TabStripModel::CommandAddToExistingGroup));
  for (size_t i = 0; i < tasks->GetItemCount(); ++i) {
    if (tasks->GetLabelAt(i) == u"Bugs") {
      tasks->ActivatedAt(i);
    }
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && InStep(first, third) &&
           TabAt(second, kIssue)->GetGroup() ==
               TabAt(second, kIssues)->GetGroup();
  }));
  for (Browser* window : {first, second, third}) {
    EXPECT_EQ(Count(window, kIssue), 1);
  }
}

// Move Tab to Another Window lists only windows on screen, not a
// workspace's hidden window or the run window.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       MoveToAnotherWindowListsOnlyShownWindows) {
  Browser* first = browser();
  tabs::TabInterface* mail = Open(first, kMail);
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal =
      list.Add("Personal", std::string(list.UnusedIcon()),
               first->GetProfile()->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->WithPartner(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* opened) { *partner = opened; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil([&] { return partner; }));
  ASSERT_TRUE(WorkspaceWindow::IsHidden(partner));
  ASSERT_TRUE(OpenRunTab(first->GetProfile()));

  TabStripModel* tabs = first->tab_strip_model();
  {
    TabMenuModel menu(nullptr, first->GetFeatures().tab_menu_model_delegate(),
                      tabs, tabs->GetIndexOfTab(mail));
    EXPECT_FALSE(
        menu.GetIndexOfCommandId(TabStripModel::CommandMoveToExistingWindow));
    EXPECT_TRUE(
        menu.GetIndexOfCommandId(TabStripModel::CommandMoveTabsToNewWindow));
  }

  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  TabMenuModel menu(nullptr, first->GetFeatures().tab_menu_model_delegate(),
                    tabs, tabs->GetIndexOfTab(mail));
  EXPECT_EQ(ItemsOf(menu, TabStripModel::CommandMoveToExistingWindow).size(),
            1u);
}

// A task folded in one window folds in the other, where the selected tab
// keeps its row, until the user picks another tab in it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AFoldedTaskKeepsTheSelectedRow) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  const std::optional<PageId> mail =
      ControllerOf(first)->IdFor(TabAt(first, kMail));
  const std::optional<PageId> issues =
      ControllerOf(first)->IdFor(TabAt(first, kIssues));
  ASSERT_TRUE(mail && issues);
  ASSERT_TRUE(ControllerOf(first)->MoveToNewTask({*mail, *issues}, u"Trip"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kIssues)->GetGroup();
  }));
  SelectByClick(second, TabAt(second, kIssues));

  TabStripModel* first_tabs = first->tab_strip_model();
  const tab_groups::TabGroupId task = *TabAt(first, kMail)->GetGroup();
  const tab_groups::TabGroupVisualData* visuals =
      first_tabs->group_model()->GetTabGroup(task)->visual_data();
  first_tabs->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(visuals->title(), visuals->color(),
                                           /*is_collapsed=*/true));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_TRUE(second->tab_strip_model()
                  ->group_model()
                  ->GetTabGroup(*TabAt(second, kMail)->GetGroup())
                  ->visual_data()
                  ->is_collapsed());
  EXPECT_EQ(second->tab_strip_model()->GetActiveTab(), TabAt(second, kIssues));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const views::View* kept = RowOf(second, TabAt(second, kIssues));
    const views::View* folded = RowOf(second, TabAt(second, kMail));
    return kept && kept->GetVisible() && !kept->size().IsEmpty() && folded &&
           !folded->GetVisible();
  }));
  SaveWindowScreenshot(second,
                       "shared-tabs-folded-task-keeps-the-selected-row");

  SelectByClick(second, TabAt(second, kMail));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && !first_tabs->IsGroupCollapsed(task);
  }));
}

// A branch folded in one window stays folded in the other, where the
// selected tab keeps its row, until the user picks another tab in it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       AFoldedBranchKeepsTheSelectedRow) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Open(first, kIssue);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  // A tab opened in the background goes right under the selected tab, so
  // Issue 412 is first, and Issues and Mail nest under it.
  TabOrganizationController* controller = ControllerOf(first);
  const std::optional<PageId> issue = controller->IdFor(TabAt(first, kIssue));
  ASSERT_TRUE(issue);
  ASSERT_TRUE(controller->Nest(*controller->IdFor(TabAt(first, kIssues))));
  ASSERT_TRUE(controller->Nest(*controller->IdFor(TabAt(first, kMail))));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) &&
           ControllerOf(second)->DepthFor(TabAt(second, kMail)) == 1;
  }));
  SelectByClick(second, TabAt(second, kMail));

  controller->ToggleBranches({*issue});
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return ControllerOf(second)->IsCollapsed(*issue); }));
  EXPECT_FALSE(ControllerOf(second)->IsHidden(TabAt(second, kMail)));
  EXPECT_TRUE(ControllerOf(second)->IsHidden(TabAt(second, kIssues)));

  SelectByClick(second, TabAt(second, kIssues));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !controller->IsCollapsed(*issue); }));
}

// After a restart, each window comes back with its selected tab and each page
// once, its live page in one window and a copy in the other.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       PRE_EachWindowComesBackWithItsSelectedTab) {
  WaitForSavedGroups();
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, GURL(kMail)));
  Open(first, kIssues);
  Open(first, kNotes);
  TabOrganizationController* controller = ControllerOf(first);
  ASSERT_TRUE(
      controller->MoveToNewTask({*controller->IdFor(TabAt(first, kMail)),
                                 *controller->IdFor(TabAt(first, kIssues))},
                                u"Trip"));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabAt(second, kNotes) && InStep(first, second); }));
  SelectByClick(first, TabAt(first, kMail));
  SelectByClick(second, TabAt(second, kIssues));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(TabAt(first, kMail)) &&
           !TabOrganizationController::IsCopy(TabAt(second, kIssues)) &&
           InStep(first, second);
  }));
  // Lets the session take the windows' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       EachWindowComesBackWithItsSelectedTab) {
  Profile* profile = browser()->GetProfile();
  Browser* on_mail = nullptr;
  Browser* on_issues = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::vector<Browser*> windows = ShownWindows(profile);
    on_mail = Selecting(windows, kMail);
    on_issues = Selecting(windows, kIssues);
    return windows.size() == 2u && on_mail && on_issues &&
           on_mail != on_issues && InStep(on_mail, on_issues);
  })) << Layouts(ShownWindows(profile));
  EXPECT_EQ(WorkspaceWindow::From(on_mail)->workspace(),
            WorkspaceWindow::From(on_issues)->workspace());
  for (const char* url : {kMail, kIssues, kNotes}) {
    EXPECT_EQ(Count(on_mail, url), 1) << url;
    EXPECT_EQ(Count(on_issues, url), 1) << url;
    EXPECT_EQ(LivePages({on_mail, on_issues}, url), 1) << url;
  }
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(on_mail, kMail)));
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(on_issues, kIssues)));

  // The task is still one task: renamed in one window, it changes in both.
  for (Browser* window : {on_mail, on_issues}) {
    EXPECT_NE(Layout(window)[0].find(" Trip "), std::string::npos);
  }
  on_issues->tab_strip_model()->ChangeTabGroupVisuals(
      *TabAt(on_issues, kMail)->GetGroup(),
      tab_groups::TabGroupVisualData(u"Lisbon",
                                     tab_groups::TabGroupColorId::kGreen));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return InStep(on_mail, on_issues) &&
           Layout(on_mail)[0].find(" Lisbon ") != std::string::npos;
  })) << Layouts({on_mail, on_issues});
}

// A window closed as the last on its workspace keeps the workspace's tabs,
// hidden. After a restart it stays hidden while another window shows, and the
// next window to show the workspace takes them.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       PRE_AKeptWindowStaysHiddenAfterARestart) {
  WaitForSavedGroups();
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, GURL(kMail)));
  Open(first, kIssues);
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal = list.Add(
      "Personal", std::string(list.UnusedIcon()), profile->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->Switch(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* shown) { *partner = shown; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return partner && partner->GetWindow()->IsVisible() &&
           !first->GetWindow()->IsVisible();
  }));
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(partner->GetBrowserForMigrationOnly(), IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] { return NewWindow(known); }));

  // Closing the partner closes its group, and the first window is the last
  // on its workspace.
  partner->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !IsOpen(partner) && WorkspaceWindow::From(first)->keeper();
  }));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       AKeptWindowStaysHiddenAfterARestart) {
  BrowserWindowInterface* kept = nullptr;
  BrowserWindowInterface* shown = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    kept = shown = nullptr;
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
      if (workspace && workspace->keeper()) {
        kept = window;
      } else if (workspace && window->GetWindow()->IsVisible()) {
        shown = window;
      }
    }
    return kept && shown && TabAt(kept->GetBrowserForMigrationOnly(), kMail);
  }));
  EXPECT_FALSE(kept->GetWindow()->IsVisible());
  const std::string work = WorkspaceWindow::From(kept)->workspace();
  EXPECT_NE(WorkspaceWindow::From(shown)->workspace(), work);

  BrowserWindowInterface* opened = nullptr;
  WorkspaceWindow::From(shown)->Switch(
      work,
      base::BindOnce([](BrowserWindowInterface** opened,
                        BrowserWindowInterface* window) { *opened = window; },
                     &opened));
  Browser* window = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    window = opened ? opened->GetBrowserForMigrationOnly() : nullptr;
    return window && !IsOpen(kept) && TabAt(window, kMail) &&
           TabAt(window, kIssues);
  }));
  SelectByClick(window, TabAt(window, kIssues));
  EXPECT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* issues = TabAt(window, kIssues);
    return !TabOrganizationController::IsCopy(issues) &&
           issues->GetContents()->GetTitle() == u"Issues";
  }));
}

// `window`'s layout without the blank tab a test opens at launch.
std::vector<std::string> LayoutAfterLaunch(Browser* window) {
  std::vector<std::string> rows = Layout(window);
  std::erase_if(rows, [](const std::string& row) {
    return row.starts_with("about:blank ");
  });
  return rows;
}

// After a restart, a copy shows its page's icon before anything loads it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       PRE_ACopyShowsItsPagesIconAfterARestart) {
  WaitForSavedGroups();
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  const GURL url =
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html");
  Open(first, url.spec().c_str());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return ShowsIcon(IconPageIn(first)->GetContents()); }));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return IconPageIn(second) && ShowsIcon(IconPageIn(second)->GetContents());
  }));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ACopyShowsItsPagesIconAfterARestart) {
  Profile* profile = browser()->GetProfile();
  std::vector<Browser*> windows;
  ASSERT_TRUE(base::test::RunUntil([&] {
    windows = ShownWindows(profile);
    return windows.size() == 2u && IconPageIn(windows[0]) &&
           IconPageIn(windows[1]);
  }));
  for (Browser* window : windows) {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return RowShowsItsPage(window, IconPageIn(window));
    })) << (TabOrganizationController::IsCopy(IconPageIn(window)) ? "copy"
                                                                  : "live");
  }
}

// A page that every window holds as a copy, as once the last window closed,
// shows its icon after a restart, and so does a new window's copy of it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       PRE_APageNoWindowHoldsShowsItsIconAfterARestart) {
  WaitForSavedGroups();
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  CreateIncognitoBrowser(profile);
  const GURL url =
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html");
  Open(first, url.spec().c_str());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return ShowsIcon(IconPageIn(first)->GetContents()); }));
  first->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabOrganizationController::IsCopy(IconPageIn(first)); }));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       APageNoWindowHoldsShowsItsIconAfterARestart) {
  Profile* profile = browser()->GetProfile();
  Browser* window = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::vector<Browser*> windows = ShownWindows(profile);
    window = windows.size() == 1u ? windows[0] : nullptr;
    return window && IconPageIn(window);
  }));
  ASSERT_TRUE(TabOrganizationController::IsCopy(IconPageIn(window)));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return RowShowsItsPage(window, IconPageIn(window)); }));

  Browser* second = CreateBrowser(profile);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return IconPageIn(second) && RowShowsItsPage(second, IconPageIn(second));
  }));
}

// The first launch with shared tabs, after a session saved without them.
class SharedTabsFirstLaunchBrowserTest : public SharedTabsBrowserTest {
 public:
  SharedTabsFirstLaunchBrowserTest() {
    if (GetTestPreCount() > 0) {
      off_.InitAndDisableFeature(kUwSharedTabs);
    }
  }

 private:
  base::test::ScopedFeatureList off_;
};

IN_PROC_BROWSER_TEST_F(SharedTabsFirstLaunchBrowserTest,
                       PRE_TheFirstLaunchMergesEachWorkspacesWindows) {
  WaitForSavedGroups();
  Browser* back = browser();
  Profile* profile = back->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(back, GURL(kMail)));
  Open(back, kIssues, /*select=*/true);
  ASSERT_TRUE(ControllerOf(back)->MoveToNewTask(
      {*ControllerOf(back)->IdFor(TabAt(back, kMail))}, u"Trip"));
  Browser* front = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(front, GURL(kNotes)));
  Open(front, kIssue, /*select=*/true);
  ASSERT_TRUE(ControllerOf(front)->MoveToNewTask(
      {*ControllerOf(front)->IdFor(TabAt(front, kNotes))}, u"Trip"));
  EXPECT_FALSE(TabAt(back, kNotes));
  // Lets the session take the windows' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

// Each window's tasks and Scratchpad tabs join the workspace's tree, the
// window in front first. Two tasks of one name stay apart, and nothing loads
// twice.
IN_PROC_BROWSER_TEST_F(SharedTabsFirstLaunchBrowserTest,
                       TheFirstLaunchMergesEachWorkspacesWindows) {
  Profile* profile = browser()->GetProfile();
  Browser* back = nullptr;
  Browser* front = nullptr;
  // Each window holds the live pages it had.
  const auto holds = [](Browser* window, const char* url) {
    return TabAt(window, url) &&
           !TabOrganizationController::IsCopy(TabAt(window, url));
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::vector<Browser*> windows = ShownWindows(profile);
    back = front = nullptr;
    for (Browser* window : windows) {
      back = holds(window, kMail) ? window : back;
      front = holds(window, kNotes) ? window : front;
    }
    return windows.size() == 2u && back && front && back != front &&
           LayoutAfterLaunch(back).size() == 4u && InStep(back, front);
  })) << Layouts(ShownWindows(profile));
  // The launch opened its blank tab in front, so only the window behind still
  // shows the tab it had selected.
  EXPECT_EQ(back->tab_strip_model()->GetActiveTab(), TabAt(back, kIssues));
  const std::vector<std::string> merged = {
      std::string(kNotes) + " Trip 0", std::string(kMail) + " Trip 0",
      std::string(kIssue) + " - 0", std::string(kIssues) + " - 0"};
  EXPECT_EQ(LayoutAfterLaunch(back), merged);
  EXPECT_NE(TabAt(back, kNotes)->GetGroup(), TabAt(back, kMail)->GetGroup());
  for (const char* url : {kMail, kIssues, kNotes, kIssue}) {
    EXPECT_EQ(LivePages({back, front}, url), 1) << url;
  }
}

class SharedTabsOffBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  SharedTabsOffBrowserTest() { features_.InitAndDisableFeature(kUwSharedTabs); }

 private:
  base::test::ScopedFeatureList features_;
};

// Close Task and Clear leave a blank tab instead of closing a window's last
// tabs. Pins do not count.
IN_PROC_BROWSER_TEST_F(SharedTabsOffBrowserTest,
                       CloseTaskAndClearLeaveABlankTabBesidePins) {
  Browser* window = browser();
  TabStripModel* tabs = window->tab_strip_model();
  TabOrganizationController* controller = ControllerOf(window);
  content::WebContents* pinned =
      chrome::AddAndReturnTabAt(window, GURL(kMail), -1, false);
  ASSERT_TRUE(content::WaitForLoadStop(pinned));
  tabs->SetTabPinned(tabs->GetIndexOfWebContents(pinned), true);
  const auto regular = [&] {
    return tabs->count() - tabs->IndexOfFirstNonPinnedTab();
  };
  ASSERT_EQ(regular(), 1);

  const std::optional<PageId> only =
      controller->IdFor(tabs->GetTabAtIndex(tabs->IndexOfFirstNonPinnedTab()));
  const std::optional<tab_groups::TabGroupId> task =
      controller->MoveToNewTask({*only}, u"Trip");
  ASSERT_TRUE(task);
  controller->CloseTask(*task);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return regular() == 1 &&
           !tabs->GetTabAtIndex(tabs->IndexOfFirstNonPinnedTab())->GetGroup();
  }));

  const content::WebContents* blank =
      tabs->GetWebContentsAt(tabs->IndexOfFirstNonPinnedTab());
  controller->ClearScratchpad();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return regular() == 1 &&
           tabs->GetWebContentsAt(tabs->IndexOfFirstNonPinnedTab()) != blank;
  }));
  EXPECT_TRUE(IsOpen(window));
}

IN_PROC_BROWSER_TEST_F(SharedTabsOffBrowserTest, EachWindowKeepsItsOwnTabs) {
  Browser* first = browser();
  content::WebContents* mail =
      chrome::AddAndReturnTabAt(first, GURL(kMail), -1, false);
  ASSERT_TRUE(content::WaitForLoadStop(mail));
  Browser* second = CreateBrowser(first->GetProfile());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(second, kMail));
  EXPECT_EQ(Layout(second).size(), 1u);
}

}  // namespace
}  // namespace uw
