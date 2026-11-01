# Windows uw updates with a fetcher of its own behind Chromium's relaunch button

On 2026-11-01 the user asked for Windows updates that behave like the Mac's: a daily check, a download in the background, Chromium's "Relaunch to update", and no updater windows ([ADR 0019](0019-sparkle-updates-behind-chromiums-relaunch-button.md)). On Windows, Chromium already does most of this. Its installer, `mini_installer`, installs silently. Run while uw is open, it stages the new version, and Chromium swaps it in at the next relaunch. The relaunch chip appears once the newer version is in the registry. So uw adds only a small fetcher: once a day it reads a feed on the latest GitHub release, downloads the installer, checks its signature, and runs it.

Considered options:
- WinSparkle, rejected: it reads Sparkle's appcast format, but it always shows its own window when it finds an update.
- Velopack, rejected: it brings its own installer and feed, so uw would redo the browser registration and update swap `mini_installer` already does.
- Chromium Updater, rejected: it needs an Omaha server that signs each answer.

Consequences: both platforms need `enable_update_notifications`, which unbranded Chromium leaves off. Every Windows release needs a higher four-part `chrome/VERSION`, because setup installs only a higher version. Moving to another updater later means shipping one last release whose fetcher installs it.
