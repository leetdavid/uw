// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>
#include <optional>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
#include "chrome/browser/actor/enterprise_policy_checker.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/performance_manager/policies/page_discarding_helper.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/actor.mojom.h"
#include "components/actor/core/task_source_info.h"
#include "components/actor/public/mojom/actor_types.mojom.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace uw {
namespace {

using performance_manager::PerformanceManager;
using performance_manager::policies::CannotDiscardReason;
using performance_manager::policies::DiscardEligibilityPolicy;
using performance_manager::policies::PageDiscardingHelper;
using ::testing::Contains;
using ::testing::Not;

bool UnloadAsMemorySaverDoes(tabs::TabInterface* tab) {
  base::WeakPtr<performance_manager::PageNode> node =
      PerformanceManager::GetPrimaryPageNodeForWebContents(tab->GetContents());
  return node &&
         PageDiscardingHelper::GetFromGraph(PerformanceManager::GetGraph())
             ->ImmediatelyDiscardMultiplePages(
                 {node.get()},
                 DiscardEligibilityPolicy::DiscardReason::PROACTIVE);
}

std::vector<CannotDiscardReason> ReasonsToKeep(tabs::TabInterface* tab) {
  std::vector<CannotDiscardReason> reasons;
  base::WeakPtr<performance_manager::PageNode> node =
      PerformanceManager::GetPrimaryPageNodeForWebContents(tab->GetContents());
  if (node) {
    DiscardEligibilityPolicy::GetFromGraph(PerformanceManager::GetGraph())
        ->CanDiscard(node.get(),
                     DiscardEligibilityPolicy::DiscardReason::PROACTIVE,
                     /*ignore_recent_visibility=*/true, &reasons);
  }
  return reasons;
}

class UnloadedTabsBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

 protected:
  tabs::TabInterface* OpenInBackground() {
    content::WebContents* page = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/false);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    return tabs::TabInterface::GetFromContents(page);
  }
};

IN_PROC_BROWSER_TEST_F(UnloadedTabsBrowserTest, MemorySaverIsOnAtModerate) {
  namespace prefs = performance_manager::user_tuning::prefs;
  const PrefService* local_state = g_browser_process->local_state();
  EXPECT_EQ(local_state->GetInteger(prefs::kMemorySaverModeState),
            static_cast<int>(prefs::MemorySaverModeState::kEnabled));
  EXPECT_EQ(
      local_state->GetInteger(prefs::kMemorySaverModeAggressiveness),
      static_cast<int>(prefs::MemorySaverModeAggressiveness::kConservative));
  EXPECT_TRUE(performance_manager::user_tuning::UserPerformanceTuningManager::
                  GetInstance()
                      ->IsMemorySaverModeActive());
}

IN_PROC_BROWSER_TEST_F(UnloadedTabsBrowserTest, ATabTheAgentActsInStaysLoaded) {
  tabs::TabInterface* tab = OpenInBackground();
  ASSERT_THAT(ReasonsToKeep(tab),
              Not(Contains(CannotDiscardReason::kAgentActing)));

  actor::ActorKeyedService* agents =
      actor::ActorKeyedService::Get(browser()->GetProfile());
  const actor::TaskId task = agents->CreateTask(
      actor::TaskSourceInfo(actor::TaskSourceInfo::Client::kExperimentalActor,
                            std::nullopt),
      actor::GetNullEnterprisePolicyChecker());
  base::test::TestFuture<actor::mojom::ActionResultPtr> added;
  agents->GetTask(task)->AddTab(tab->GetHandle(),
                                /*stop_task_on_detach=*/true,
                                added.GetCallback());
  ASSERT_EQ(added.Get()->code, actor::mojom::ActionResultCode::kOk);

  EXPECT_THAT(ReasonsToKeep(tab), Contains(CannotDiscardReason::kAgentActing));
  EXPECT_FALSE(UnloadAsMemorySaverDoes(tab));
  EXPECT_FALSE(tab->GetContents()->WasDiscarded());

  agents->StopTask(task, actor::ActorTask::StoppedReason::kTaskComplete);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !std::ranges::contains(ReasonsToKeep(tab),
                                  CannotDiscardReason::kAgentActing);
  }));
  EXPECT_TRUE(UnloadAsMemorySaverDoes(tab));
  EXPECT_TRUE(tab->GetContents()->WasDiscarded());
}

}  // namespace
}  // namespace uw
