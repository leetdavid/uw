// Copyright 2026 The uw Authors. All rights reserved.

// About uw asks uw's own updater instead of Chromium's Google one, which uw
// does not ship. See docs/plans/distribution.md, "Updates".

#include "chrome/browser/ui/webui/help/version_updater.h"

#include <memory>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "build/build_config.h"
#include "chrome/browser/upgrade_detector/upgrade_detector.h"
#include "chrome/grit/branded_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/update/update_check.h"

namespace {

VersionUpdater::Status StatusOf(uw::UpdateState state) {
  switch (state) {
    case uw::UpdateState::kIdle:
    case uw::UpdateState::kChecking:
      return VersionUpdater::CHECKING;
    case uw::UpdateState::kDownloading:
      return VersionUpdater::UPDATING;
    case uw::UpdateState::kReady:
      return VersionUpdater::NEARLY_UPDATED;
    case uw::UpdateState::kUpToDate:
      return VersionUpdater::UPDATED;
    case uw::UpdateState::kFailed:
      return VersionUpdater::FAILED;
  }
}

class UwVersionUpdater : public VersionUpdater {
 public:
  void CheckForUpdate(StatusCallback status_callback,
                      PromoteCallback promote_callback) override {
    status_callback_ = std::move(status_callback);
    if (uw::HasUpdateChecker() && !UpdateWaits()) {
      watching_ = uw::WatchUpdateState(
          base::BindRepeating(&UwVersionUpdater::Show, base::Unretained(this)));
      uw::CheckForUpdateNow();
    }
    Show(uw::CurrentUpdateState());
  }

#if BUILDFLAG(IS_MAC)
  void PromoteUpdater() override {}
#endif

 private:
  static bool UpdateWaits() {
    return UpgradeDetector::GetInstance()->is_upgrade_available();
  }

  void Show(uw::UpdateState state) {
    Status status = UpdateWaits()            ? NEARLY_UPDATED
                    : uw::HasUpdateChecker() ? StatusOf(state)
                                             : DISABLED;
    status_callback_.Run(status, 0, false, false, std::string(), 0,
                         status == FAILED
                             ? l10n_util::GetStringUTF16(IDS_UW_UPDATE_FAILED)
                             : std::u16string());
  }

  StatusCallback status_callback_;
  base::CallbackListSubscription watching_;
};

}  // namespace

std::unique_ptr<VersionUpdater> VersionUpdater::Create(
    content::WebContents* web_contents) {
  return std::make_unique<UwVersionUpdater>();
}
