// Copyright 2026 The uw Authors. All rights reserved.

#include <optional>

#include "base/functional/bind.h"
#include "base/version.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/updater/updater.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "uw/browser/update/pending_update.h"
#include "uw/browser/update/sparkle_updater.h"

namespace uw {
namespace {

class UpdateBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    ClearPendingUpdateForTesting();
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

IN_PROC_BROWSER_TEST_F(UpdateBrowserTest, RelaunchHandsTheInstallToSparkle) {
  DownloadUpdate();

  chrome::AttemptRestart();

  EXPECT_EQ(installs_, 1);
  EXPECT_TRUE(local_state().GetBoolean(prefs::kWasRestarted));
  EXPECT_FALSE(local_state().GetBoolean(prefs::kRestartLastSessionOnShutdown));
}

}  // namespace
}  // namespace uw
