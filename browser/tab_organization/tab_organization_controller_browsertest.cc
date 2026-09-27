// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "crypto/apple/scoped_fake_keychain_v2.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"

namespace uw {
namespace {

class TabOrganizationControllerBrowserTest : public InProcessBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  tabs::TabInterface* AddTab(const GURL& url) {
    int index = browser()->tab_strip_model()->count();
    EXPECT_TRUE(AddTabAtIndex(index, url, ui::PAGE_TRANSITION_TYPED));
    return browser()->tab_strip_model()->GetTabAtIndex(index);
  }

  TreeId PageId(tabs::TabInterface* tab) {
    auto id = controller()->IdFor(tab);
    EXPECT_TRUE(id);
    return *id;
  }
};

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ControllerExistsForNormalWindow) {
  ASSERT_TRUE(controller());
  auto* initial = browser()->tab_strip_model()->GetTabAtIndex(0);
  auto* tab = AddTab(GURL("about:blank"));
  ASSERT_TRUE(initial);
  ASSERT_TRUE(tab);
  // A sourceless new tab opens directly beneath the previously active tab
  // as its sibling.
  const TabTree::Node* parent =
      controller()->tree().Find(PageId(tab))->parent();
  EXPECT_EQ(parent, controller()->tree().scratchpad());
  EXPECT_EQ(parent->GetIndexOf(controller()->tree().Find(PageId(tab))).value(),
            parent->GetIndexOf(controller()->tree().Find(PageId(initial)))
                    .value() +
                1);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       DisabledByDefaultKeepsTabsInScratchpad) {
  EXPECT_FALSE(
      browser()->GetProfile()->GetPrefs()->GetBoolean(kOrganizationEnabledPref));
  auto* first = AddTab(GURL("about:blank"));
  auto* second = AddTab(GURL("about:blank"));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  base::RunLoop().RunUntilIdle();
  // Without configuration nothing is grouped into tasks.
  EXPECT_TRUE(controller()->tree().TaskFor(PageId(first)) == nullptr);
  EXPECT_TRUE(controller()->tree().TaskFor(PageId(second)) == nullptr);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       NewTabsOpenAsSiblingsOfPreviouslyActiveTab) {
  auto* initial = browser()->tab_strip_model()->GetTabAtIndex(0);
  ASSERT_TRUE(initial);
  const TreeId initial_id = PageId(initial);
  auto* first = AddTab(GURL("about:blank"));
  auto* second = AddTab(GURL("about:blank"));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  // Each sourceless new tab opens directly beneath the tab that was active
  // before it, at the same nesting level.
  const TabTree::Node* first_node =
      controller()->tree().Find(PageId(first));
  const TabTree::Node* second_node =
      controller()->tree().Find(PageId(second));
  EXPECT_EQ(first_node->parent(), controller()->tree().Find(initial_id)->parent());
  EXPECT_EQ(second_node->parent(), first_node->parent());
  EXPECT_EQ(first_node->parent()->GetIndexOf(second_node).value(),
            first_node->parent()->GetIndexOf(first_node).value() + 1);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ManualMakeTaskPreservesLivePages) {
  auto* first = AddTab(GURL("about:blank"));
  auto* second = AddTab(GURL("about:blank"));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  content::WebContents* first_contents = first->GetContents();
  content::WebContents* second_contents = second->GetContents();
  ASSERT_TRUE(first_contents);
  ASSERT_TRUE(second_contents);
  const int selected_before =
      browser()->tab_strip_model()->active_index();
  auto task = controller()->MakeTask(
      {PageId(first), PageId(second)}, u"Manual");
  ASSERT_TRUE(task);
  EXPECT_EQ(first->GetContents(), first_contents);
  EXPECT_EQ(second->GetContents(), second_contents);
  EXPECT_EQ(browser()->tab_strip_model()->active_index(),
            selected_before);
  EXPECT_EQ(controller()->tree().Find(PageId(first))->parent(),
            controller()->tree().Find(*task));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       UnreachableProvidersLeaveTabsPut) {
  crypto::apple::ScopedFakeKeychainV2 fake_keychain("uw-test");
  ASSERT_TRUE(ApiKeyStore().SetKey("organization", "test-key"));
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  // Refused connections fail fast without touching the network.
  prefs->SetInteger(kOrganizationModePref,
                    static_cast<int>(ConnectionMode::kOpenAiCompatible));
  prefs->SetString(kOrganizationEndpointPref, "http://127.0.0.1:9/");
  prefs->SetString(kOrganizationModelPref, "test-model");
  prefs->SetString(kOrganizationKeyAccountPref, "organization");
  prefs->SetBoolean(kOrganizationEnabledPref, true);
  auto* initial = browser()->tab_strip_model()->GetTabAtIndex(0);
  auto* tab = AddTab(GURL("about:blank"));
  ASSERT_TRUE(initial);
  ASSERT_TRUE(tab);
  controller()->ClassifyNowForTesting();
  base::RunLoop().RunUntilIdle();
  // Provider failure is a normal discard: the tab keeps its sibling
  // placement instead of moving into a task.
  EXPECT_EQ(controller()->tree().Find(PageId(tab))->parent(),
            controller()->tree().scratchpad());
  EXPECT_TRUE(controller()->tree().TaskFor(PageId(tab)) == nullptr);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PauseStopsAutomaticOrganization) {
  crypto::apple::ScopedFakeKeychainV2 fake_keychain("uw-test");
  ASSERT_TRUE(ApiKeyStore().SetKey("organization", "test-key"));
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetInteger(kOrganizationModePref,
                    static_cast<int>(ConnectionMode::kOpenAiCompatible));
  prefs->SetString(kOrganizationEndpointPref, "http://127.0.0.1:9/");
  prefs->SetString(kOrganizationModelPref, "test-model");
  prefs->SetString(kOrganizationKeyAccountPref, "organization");
  prefs->SetBoolean(kOrganizationEnabledPref, true);
  auto* tab = AddTab(GURL("about:blank"));
  ASSERT_TRUE(tab);

  controller()->SetPause(PauseMode::kIndefinite);
  EXPECT_EQ(controller()->pause_mode(), PauseMode::kIndefinite);
  controller()->ClassifyNowForTesting();
  base::RunLoop().RunUntilIdle();
  // Paused: nothing organizes and no pending result is applied.
  EXPECT_TRUE(controller()->tree().TaskFor(PageId(tab)) == nullptr);

  controller()->SetPause(PauseMode::kOff);
  EXPECT_EQ(controller()->pause_mode(), PauseMode::kOff);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TimedPauseExpires) {
  controller()->SetPause(PauseMode::kTimed, base::Seconds(0));
  EXPECT_EQ(controller()->pause_mode(), PauseMode::kOff);
  controller()->SetPause(PauseMode::kUntilLaunch);
  EXPECT_EQ(controller()->pause_mode(), PauseMode::kUntilLaunch);
  controller()->SetPause(PauseMode::kOff);
  EXPECT_EQ(controller()->pause_mode(), PauseMode::kOff);
}

}  // namespace
}  // namespace uw
