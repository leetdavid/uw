# Sparkle updates uw behind Chromium's relaunch button

On 2026-10-26 the user asked for "restart to update", as Chrome does. Chrome's button comes from `UpgradeDetector`, which notices when the app on disk is newer than the running one; its engine, Chromium Updater, is built only for Chrome-branded builds and talks to an Omaha-protocol server. We chose Sparkle 2 as the engine: it checks a feed published with each GitHub release, downloads and verifies the update in the background, and then tells `UpgradeDetector` a newer version is ready. Chromium's own "Relaunch to update" chip and menu item install it, and Sparkle's windows never show.

Considered options: Chromium Updater with uw branding (rejected: a separate updater app, launchd jobs, Keystone-style install packages, and an Omaha server to run); Sparkle with its own dialogs (rejected: an update prompt that does not look like the rest of the browser).

Consequences: uw carries a patch that hands Sparkle's ready update to `UpgradeDetector` and lets Chromium's relaunch finish Sparkle's install. Every release needs a feed entry signed with an EdDSA key kept on the build Mac. Moving to another updater later means shipping one last Sparkle update that installs it.
