# Distribution

How people other than the owner get uw: GitHub releases, a placeholder website, and what each download promises. Under discussion since 2026-10-26; sections below are agreed unless marked open.

## Audience

Agreed: an open early preview. Anyone can download it from GitHub or the website. It is marked as rough, may break, and comes with no support promise. uw is still built for the owner's daily browsing first ([Audience](../product.md#audience)).

## Signing

Agreed: every preview is signed with a Developer ID and notarized by Apple, so it opens without the Open Anyway steps and keeps its Keychain access and macOS permissions across updates. The owner joins the Apple Developer Program as an individual ($99 a year) before the first public preview.

## Updates

Agreed: Sparkle 2 checks for, downloads, and verifies updates in the background. When one is ready, Chromium's own "Relaunch to update" chip and menu item appear, and relaunching installs it with the user's tabs restored. Sparkle's own windows never show. The feed, `appcast.xml`, is an asset of each GitHub release. See [ADR 0019](../adr/0019-sparkle-updates-behind-chromiums-relaunch-button.md).

## Open questions

- Release pipeline and build flags
- Versions and channels
- Platforms and package format
- License, Chromium notices, codecs, name, and bundle ID
- Website
