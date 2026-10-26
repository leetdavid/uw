# Distribution

How people other than the owner get uw: GitHub releases, a placeholder website, and what each download promises. Agreed on 2026-10-26, except the defaults marked as proposed.

## Audience

Agreed: an open early preview. Anyone can download it from GitHub or the website. It is marked as rough, may break, and comes with no support promise. uw is still built for the owner's daily browsing first ([Audience](../product.md#audience)).

## Signing

Agreed: every preview is signed with a Developer ID and notarized by Apple, so it opens without the Open Anyway steps and keeps its Keychain access and macOS permissions across updates. The owner joins the Apple Developer Program as an individual ($99 a year) before the first public preview.

## Updates

Agreed: Sparkle 2 checks for, downloads, and verifies updates in the background. When one is ready, Chromium's own "Relaunch to update" chip and menu item appear, and relaunching installs it with the user's tabs restored. Sparkle's own windows never show. The feed, `appcast.xml`, is an asset of each GitHub release. See [ADR 0019](../adr/0019-sparkle-updates-behind-chromiums-relaunch-button.md).

## Release pipeline

Agreed: previews are built on the owner's Mac in the `~/dev/uw-release` worktree, not in CI. Free GitHub runners stop a job after 6 hours, which is too short for a Chromium build. One command, `pnpm run release <sha>`, runs every step: check out, build, smoke test, sign and notarize with Chromium's `sign_chrome.py`, make the DMG, sign the Sparkle feed entry, and publish the GitHub release. `sign_chrome.py` is upstream Python, like the rest of Chromium's build tools.

## Versions

Agreed: uw has its own version, starting at 0.1.0 for the first public preview. Tags are `v0.1.0` and so on. The About page and release notes name the Chromium version it is built on, as in "uw 0.1.0 (Chromium 153.0.8010.53)". Only the app bundle's version keys change; the user agent and everything websites and extensions see keep Chromium's version.

There is one channel. Each preview is a normal GitHub release rather than a prerelease, so `releases/latest/download/appcast.xml` always finds the newest feed. The release title says it is a preview.

## Platforms and package

Agreed: Apple silicon only, macOS 13 or later. Each release is one notarized DMG that opens to the usual drag-to-Applications window, and Sparkle installs updates from the same DMG. Intel Macs are not supported; a universal build would double the build time.

## License

Agreed for now: no license. The source is public to read, and all rights are reserved, which keeps every way to make money open. The owner decides on a license once monetization is clearer. Chromium's own notices ship in every build at `chrome://credits`.

## Codecs

Agreed: before the first public preview, uw plays H.264 video and AAC audio through macOS's own decoders, VideoToolbox and AudioToolbox, as Firefox does, and stops shipping FFmpeg's H.264 and AAC decoders. Apple's license covers its decoders; FFmpeg's are covered by patent pools uw has no license from. Sites such as x.com must still play their videos.

Found so far: Chromium's AudioToolbox decoder takes only xHE-AAC, AC-3, and E-AC-3, so plain AAC still goes to FFmpeg. WebRTC's H.264 (`rtc_use_h264`) builds OpenH264 from source, which Cisco's patent license does not cover. Both need work.

## Domain and bundle ID

Agreed: the website is `uwbrowser.com`, registered through Vercel. The app's bundle ID changes from `org.uw.browser`, a domain uw does not own, to `com.uwbrowser.uw` before the first public preview. macOS keys permissions, preferences, and Sparkle's state on the bundle ID, so it must not change after people install uw.

## Website

Agreed: a Next.js app in `site/`, a package in this repo's pnpm workspace, deployed by Vercel to `uwbrowser.com`. It uses no ESLint or Prettier. The download button links to `https://github.com/leetdavid/uw/releases/latest/download/uw-macos-arm64.dmg`, so every release names its DMG the same and the site needs no change per release.

## Keys in public builds

Agreed: public previews carry none of the owner's org-only sign-in app keys from [ADR 0014](../adr/0014-uw-signs-in-with-its-own-apps-kept-out-of-the-repo.md). `pnpm run release` refuses to build when they are set. Previews have no one-click GitHub, Slack, or Google sign-in; the owner's own builds keep it.

## Problem reports

Agreed: people report problems through GitHub Issues, linked from uw's Help menu and the website. Issues are welcome; pull requests are not accepted while uw has no license.

With the user's consent, uw also sends problem reports:

- Native crashes go to Sentry through Chromium's own crash reporter, Crashpad. Release builds keep symbols (`symbol_level = 1`), and `pnpm run release` uploads them to Sentry.
- uw's own React pages, such as the chat, settings, and organizer pages, send rage clicks, dead clicks, and JavaScript errors to PostHog. Text fields are masked, there is no session replay, and websites are never watched.

On first run uw asks "Send crash reports and problem reports to help fix uw?" with Yes and No and nothing pre-selected. One switch in settings changes the answer. Crash dumps can hold page contents, so they are sent only after a Yes.

The website uses PostHog for visits and download clicks.

## Build flags

Agreed: previews use the same `chromium/args.gn` as daily builds, plus `symbol_level = 1` for crash reports, so what people download matches what the owner uses. An official build with link-time and profile-guided optimization may come later, once measured.

## Website content

Agreed: one page, designed on the Doop canvas in the `uw-arc-style` look before it is built.

1. Hero: the logo, "ur web, ur way", and one line on what uw is.
2. A "Download for Mac" button with "Apple silicon · macOS 13+ · Early preview", and a GitHub link.
3. One screenshot of the sidebar with tasks.
4. Three features that work in the preview: tasks that organize themselves, the built-in agent, and terminal tabs.
5. Footer: a note that the preview may break, a privacy line (problem reports only with consent; AI requests go only to the providers the user picks), and links to GitHub Issues and the source.

No email list, docs, or pricing yet.

## Cadence

Agreed: a preview for every Chromium major version, about every 4 weeks, and one within about a week when Chromium fixes a bug that is being exploited. Other Chromium security fixes ride the next preview.

## Details

Proposed defaults, not discussed separately:

- Sparkle checks once a day and downloads on its own. Chromium's relaunch chip grows more insistent over the following days, as in Chrome.
- `pnpm run release` drafts release notes from the `feat` and `fix` commits since the last tag. The owner edits them before publishing.
- Help → "Report an issue" opens a new GitHub issue with the uw, Chromium, and macOS versions filled in.
- The `153.0.8010.53-1` prerelease stays as it is. The README's download section links to the website and the latest release.
- The website says that tasks and the agent need the user's own AI provider.

## What 0.1.0 needs

Agreed: everything in this plan ships in 0.1.0. The bundle ID and the updater cannot be added after people install, and the first users meet the most crashes.

The owner sets up, once:

- An individual Apple Developer Program membership, a Developer ID Application certificate on the build Mac, and a `notarytool` keychain profile.
- `uwbrowser.com` registered through Vercel, and a Vercel project for `site/`.
- A Sentry project for crash reports and a PostHog project for uw's pages and the website.

Work, in order, with scores from the [roadmap convention](roadmap.md#implementability-estimates):

| Work | Score | Main consideration |
| --- | --- | --- |
| Bundle ID and uw version | 8/10 | `com.uwbrowser.uw`, uw's version in the bundle, and "uw 0.1.0 (Chromium …)" on the About page. The user agent keeps Chromium's version. |
| Release command | 6/10 | Wraps `sign_chrome.py`. The `uw` command's Node helper needs its own entitlements in Chromium's signing config, and notarizing a bundle this size is slow. |
| Sparkle behind Chromium's relaunch button | 5/10 | A patch hands Sparkle's ready update to `UpgradeDetector`, and Chromium's relaunch has to finish Sparkle's install with tabs restored. |
| Problem reports and consent | 5/10 | First-run Yes/No, one settings switch, Crashpad uploads to Sentry, symbols per release, and PostHog in uw's React pages. |
| Codecs through macOS | 4/10 | Plain AAC through AudioToolbox, H.264 through VideoToolbox only, and WebRTC's H.264 without OpenH264. Several media paths, each checked on real sites. |
| Report an issue | 9/10 | One Help-menu item. |
| Website | 8/10 | Doop design first, then Next.js in `site/` on Vercel. |

Overall: 5/10. The codec work and the Sparkle hand-off carry the most risk; the rest uses tools that already exist.
