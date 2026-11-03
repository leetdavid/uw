// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UPDATE_WINDOWS_UPDATER_H_
#define UW_BROWSER_UPDATE_WINDOWS_UPDATER_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "base/version.h"

namespace base {
class CommandLine;
}

namespace uw {

// Looks for a newer uw a few minutes after launch and then once a day, as
// Sparkle does on the Mac. It downloads the installer, checks it against the
// signed feed, and runs it, which stages the new uw for Chromium's "Relaunch
// to update". A build without a feed address does nothing. See
// docs/adr/0026-windows-uw-updates-with-a-fetcher-of-its-own-behind-chromiums-relaunch-button.md.
void StartWindowsUpdater();

// The installer starts uw after an update. When that launch reaches a running
// uw and the update waits for a relaunch, uw relaunches into the new version
// instead of opening a window of the old one; otherwise `carry_on` gets the
// launch without the installer's switch. Returns false for any other launch.
bool RelaunchIntoStagedUpdate(
    const base::CommandLine& command_line,
    base::OnceCallback<void(const base::CommandLine&)> carry_on);

// What a release's feed file says about its installer.
struct UpdateFeed {
  base::Version version;
  std::string installer;
  int64_t size = 0;
  std::string sha256;
};

// The key that signs the feed, in base64. It signs Sparkle's feed on the Mac
// too, and its private half never leaves the Mac. See scripts/release.ts.
inline constexpr char kUpdateKey[] =
    "tUSDZsx93uVKuL7o7d/RMObl4n5XFcTn/G5SHNENRZk=";

// The feed, if `signature` is the base64 Ed25519 signature of `feed` by
// `key` and the feed is complete.
std::optional<UpdateFeed> ReadUpdateFeed(std::string_view feed,
                                         std::string_view signature,
                                         std::string_view key = kUpdateKey);

}  // namespace uw

#endif  // UW_BROWSER_UPDATE_WINDOWS_UPDATER_H_
