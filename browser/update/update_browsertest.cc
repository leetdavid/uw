// Copyright 2026 The uw Authors. All rights reserved.

#include <optional>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/version.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/updater/updater.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/browser/upgrade_detector/upgrade_detector.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "uw/browser/update/pending_update.h"
#include "uw/browser/update/sparkle_updater.h"
#include "uw/browser/update/update_check.h"

namespace uw {
namespace {

class UpdateBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    ClearPendingUpdateForTesting();
    ResetUpdateCheckForTesting();
    g_browser_process->GetBuildState()->SetUpdate(
        BuildState::UpdateType::kNone, base::Version(), std::nullopt);
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  // Stands in for Sparkle finishing a download; counts installs.
  void DownloadUpdate() {
    OnUpdateDownloaded(
        base::BindRepeating([](int* installs) { ++*installs; }, &installs_));
  }

  PrefService& local_state() { return *g_browser_process->local_state(); }

  int installs_ = 0;
};

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, WithoutAnUpdateNothingIsPending) {
  EXPECT_FALSE(HasPendingUpdate());
  EXPECT_FALSE(InstallPendingUpdate());
  EXPECT_EQ(updater::CurrentlyInstalledVersion(), version_info::GetVersion());
  EXPECT_EQ(g_browser_process->GetBuildState()->update_type(),
            BuildState::UpdateType::kNone);
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, DownloadedUpdateAsksForRelaunch) {
  DownloadUpdate();

  EXPECT_TRUE(HasPendingUpdate());
  EXPECT_EQ(g_browser_process->GetBuildState()->update_type(),
            BuildState::UpdateType::kNormalUpdate);
  EXPECT_FALSE(updater::CurrentlyInstalledVersion().IsValid());
  EXPECT_EQ(installs_, 0);
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest,
                       DownloadedUpdateShowsRelaunchToUpdate) {
  DownloadUpdate();

  EXPECT_TRUE(UpgradeDetector::GetInstance()->is_upgrade_available());
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, RelaunchHandsTheInstallToSparkle) {
  DownloadUpdate();

  chrome::AttemptRestart();

  EXPECT_EQ(installs_, 1);
  EXPECT_TRUE(local_state().GetBoolean(prefs::kWasRestarted));
  EXPECT_FALSE(local_state().GetBoolean(prefs::kRestartLastSessionOnShutdown));
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, ADownloadedUpdateIsReady) {
  DownloadUpdate();
  EXPECT_EQ(CurrentUpdateState(), UpdateState::kReady);

  OnSparkleCheckFinished(/*failed=*/false);
  EXPECT_EQ(CurrentUpdateState(), UpdateState::kReady);
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, FindingNoNewerVersionIsUpToDate) {
  OnSparkleCheckFinished(/*failed=*/false);

  EXPECT_EQ(CurrentUpdateState(), UpdateState::kUpToDate);
}

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, AnyOtherSparkleErrorFailsTheCheck) {
  OnSparkleCheckFinished(/*failed=*/true);

  EXPECT_EQ(CurrentUpdateState(), UpdateState::kFailed);
}

class OldBuildBrowserTest : public UpdateBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    UpdateBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kSimulateOutdated, "2020-01-01");
  }
};

IN_PROC_BROWSER_TEST_F(OldBuildBrowserTest, AnOldBuildIsNotCalledOutOfDate) {
  content::RunAllTasksUntilIdle();

  EXPECT_FALSE(UpgradeDetector::GetInstance()->is_outdated_install());
  EXPECT_FALSE(UpgradeDetector::GetInstance()->is_outdated_install_no_au());
}

}  // namespace
}  // namespace uw
