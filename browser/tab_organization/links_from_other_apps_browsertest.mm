// Copyright 2026 The uw Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <optional>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/app_controller_mac.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/base/apple/url_conversions.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

class LinksFromOtherAppsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  PageId Id(tabs::TabInterface* tab) {
    return controller()->IdFor(tab).value_or(PageId());
  }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("chrome://version/?link"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  // Hands `urls` to uw the way macOS does when another app opens them.
  void OpenFromAnotherApp(const std::vector<GURL>& urls) {
    NSMutableArray<NSURL*>* list = [NSMutableArray array];
    for (const GURL& url : urls) {
      [list addObject:net::NSURLWithGURL(url)];
    }
    const int before = tabs()->count();
    [AppController.sharedController application:NSApp openURLs:list];
    ASSERT_TRUE(base::test::RunUntil([&] {
      return tabs()->count() == before + static_cast<int>(urls.size());
    }));
    base::RunLoop().RunUntilIdle();
  }

  tabs::TabInterface* TabShowing(const GURL& url) {
    for (int i = 0; i < tabs()->count(); ++i) {
      tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
      if (tab->GetContents()->GetVisibleURL() == url) {
        return tab;
      }
    }
    return nullptr;
  }

  std::vector<PageId> Strip() {
    std::vector<PageId> ids;
    for (int i = 0; i < tabs()->count(); ++i) {
      ids.push_back(Id(tabs()->GetTabAtIndex(i)));
    }
    return ids;
  }
};

IN_PROC_BROWSER_TEST_F(LinksFromOtherAppsBrowserTest,
                       ALinkFromAnotherAppOpensAtTheTopOfTheScratchpad) {
  ASSERT_TRUE(AppController.sharedController.startupComplete);
  tabs::TabInterface* old = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* page = OpenLinkFrom(old);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Plan"));
  base::RunLoop().RunUntilIdle();
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(page));
  const std::optional<tab_groups::TabGroupId> plan = page->GetGroup();
  ASSERT_TRUE(plan);

  const GURL first("chrome://version/?first");
  const GURL second("chrome://version/?second");
  OpenFromAnotherApp({first, second});
  tabs::TabInterface* a = TabShowing(first);
  tabs::TabInterface* b = TabShowing(second);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(a), Id(b), Id(old)}));
  EXPECT_FALSE(a->GetGroup());
  EXPECT_FALSE(b->GetGroup());
  EXPECT_EQ(controller()->DepthFor(a), 0);
  EXPECT_EQ(controller()->DepthFor(b), 0);
  EXPECT_EQ(tabs()->GetActiveTab(), a);

  // A tab opened in uw still opens beside the active tab, in its task.
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(page));
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  base::RunLoop().RunUntilIdle();
  tabs::TabInterface* blank = tabs()->GetActiveTab();
  EXPECT_EQ(blank->GetGroup(), plan);
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(page), Id(blank), Id(a), Id(b), Id(old)}));
}

}  // namespace
}  // namespace uw
