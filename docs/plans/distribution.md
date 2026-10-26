# Distribution

How people other than the owner get uw: GitHub releases, a placeholder website, and what each download promises. Agreed on 2026-10-26, except the defaults marked as proposed.

## Audience

Agreed: an open early preview. Anyone can download it from GitHub or the website. It is marked as rough, may break, and comes with no support promise. uw is still built for the owner's daily browsing first ([Audience](../product.md#audience)).

## Signing

Agreed: every preview is signed with a Developer ID and notarized by Apple, so it opens without the Open Anyway steps and keeps its Keychain access and macOS permissions across updates. The owner joins the Apple Developer Program as an individual ($99 a year) before the first public preview.

## Updates

Agreed: Sparkle 2 checks for, downloads, and verifies updates in the background. When one is ready, Chromium's own "Relaunch to update" chip and menu item appear, and relaunching installs it with the user's tabs restored. Sparkle's own windows never show. The feed, `appcast.xml`, is an asset of each GitHub release. Only releases carry Sparkle: `pnpm run release` adds the framework and the feed's address, so daily builds never check for updates. See [ADR 0019](../adr/0019-sparkle-updates-behind-chromiums-relaunch-button.md).

## Release pipeline

Agreed: previews are built on the owner's Mac in the `~/dev/uw-release` worktree, not in CI. Free GitHub runners stop a job after 6 hours, which is too short for a Chromium build. One command, `pnpm run release <sha>`, runs every step: check out, build, smoke test, sign and notarize with Chromium's `sign_chrome.py`, make the DMG, sign the Sparkle feed entry, and publish the GitHub release. `sign_chrome.py` is upstream Python, like the rest of Chromium's build tools.

## Versions

Agreed: uw has its own version, starting at 0.1.0 for the first public preview. Tags are `v0.1.0` and so on. `pnpm run release` writes the version into the app bundle's `Info.plist` before signing, as Chromium's own signing scripts change the plist for each channel. The About page then reads "Version 0.1.0 (Chromium 153.0.8010.53)", and the release notes name the Chromium version too. Daily builds carry Chromium's version and show it as before. The user agent and everything websites and extensions see keep Chromium's version.

There is one channel. Each preview is a normal GitHub release rather than a prerelease, so `releases/latest/download/appcast.xml` always finds the newest feed. The release title says it is a preview.

## Platforms and package

Agreed: Apple silicon only, macOS 13 or later. Each release is one notarized DMG that opens to the usual drag-to-Applications window, and Sparkle installs updates from the same DMG. Intel Macs are not supported; a universal build would double the build time.

## Homebrew

Agreed on 2026-10-27: `brew install --cask leetdavid/uw/uw` installs uw. The cask lives in its own tap, [`leetdavid/homebrew-uw`](https://github.com/leetdavid/homebrew-uw), and points at the same latest-release DMG as the website, so no release has to change it. Sparkle updates uw after that, and `brew upgrade` leaves it alone. The full name in the command also makes Homebrew trust this one cask, which it requires for casks outside its own taps. The cask does not link the `uw` command; uw's settings install it ([uw command plan](uw-command.md)).

Homebrew's own list, for `brew install --cask uw`, takes a cask that its owner submits only once the repository has 225 stars, 90 forks, or 90 watchers.

## License

Agreed for now: no license. The source is public to read, and all rights are reserved, which keeps every way to make money open. The owner decides on a license once monetization is clearer. Chromium's own notices ship in every build at `chrome://credits`.

## Codecs

Agreed: before the first public preview, uw plays H.264 video and AAC audio through macOS's own decoders, VideoToolbox and AudioToolbox, as Firefox does, and stops shipping FFmpeg's H.264 and AAC decoders. Apple's license covers its decoders; FFmpeg's are covered by patent pools uw has no license from. Sites such as x.com must still play their videos.

Found so far, at Chromium 153 on 2026-10-27:

- `ffmpeg_branding = "Chromium"` drops FFmpeg's H.264 and AAC decoders, their parsers, and the raw AAC demuxer. MP4 demuxing stays. A patch must delete the assert in `media/BUILD.gn` that forbids this with `proprietary_codecs`. Raw `.aac` files and ADTS radio streams played with `src=` stop working.
- With `enable_ffmpeg_video_decoders = false`, H.264 plays only through VideoToolbox in the GPU process. Videos under 360 px tall, which FFmpeg decodes today, move to VideoToolbox too. With the GPU process off, H.264 does not play.
- Chromium's AudioToolbox decoder takes only xHE-AAC, AC-3, and E-AC-3. Plain AAC needs its esds magic cookie, ADTS headers stripped, HE-AAC's rate and channel changes, and priming trimmed once.
- `media_use_openh264 = false` turns off WebRTC's H.264 (`rtc_use_h264`), which builds OpenH264, a library Cisco's patent license does not cover. WebRTC still receives H.264 through VideoToolbox, but it sends H.264 only after a patch to `webrtc_util.cc`.

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

Found so far, at Chromium 153 on 2026-10-27:

- Crashpad runs in unbranded builds but has no upload address, and every launch turns uploads off (`ApplyMetricsReportingPolicy`). A patch gives `GetUploadUrl` Sentry's minidump address, reads the answer from the user data folder's "Consent To Send Stats" file, and stops the reset. Crashpad sends at most one report an hour unless started with `--no-rate-limit`.
- Release symbols need `enable_dsyms = true` beside `symbol_level = 1`. The dSYMs may run to gigabytes.
- The React pages are the chat page, `uw://ai`, and the watch card. Settings and the organizer page are Lit. Chromium gives WebUI pages that use Mojo no network access, so a page reports its problems over Mojo, and C++ sends them to PostHog.
- Chromium's own consent switch and first-run dialog appear only in Google Chrome builds, so uw adds its own.

## Build flags

Agreed: previews use the same `chromium/args.gn` as daily builds, plus `symbol_level = 1` for crash reports, so what people download matches what the owner uses. An official build with link-time and profile-guided optimization may come later, once measured.

## Website content

Agreed: one page that sells uw, in the `uw-arc-style` look. The approved design is frame `KvI1yY00T8` on the Doop canvas. It shows only uw's own interface, never real or made-up websites, and only features that are built.

1. Hero: "ur web, ur way", one line on what uw is, and "Download for Mac" with "Apple silicon · macOS 13 or later". Below it plays the ad, "Tabs, sorted" (frame `kSVeaeuYZm`): the full 28 s spot, built as animated HTML rather than a video file. It starts on its own and loops. It has no sound yet; an unmute button comes once the music exists. Where the storyboard shows the made-up food page (panels 5 and 7), the ad shows a stock photo of pastéis de nata instead, with no site name or page around it.
2. How it works: a pinned scroll demo of a live sidebar. Over four stops its tabs move out of a messy Scratchpad into tasks, a new task forms and gets its name, and Undo appears. Phones and reduced-motion settings get four still cards instead.
3. Features: the mini player docked in the sidebar, the command bar, workspaces and themes, the agent, watching a page, and the terminal.
4. Four promises: built on Chromium, the user's own AI provider, problem reports only with consent, and Undo for every move.
5. A closing section that says the preview is rough, with the download button and, beside it, the [Homebrew](#homebrew) command with a copy button. Then a footer linking releases, GitHub Issues, and the source.

Motion: the dawn background behind the hero, the demo, and the closing panel moves slowly. Its hills drift at different speeds, the pink glow breathes, and the light along the ridge glints now and then. One cycle takes about a minute.

Sections fade and rise in as they scroll into view. Each feature tile plays a short loop of its feature while on screen: the docked video plays, the command bar types and answers, the workspaces swipe, the agent's Yes gets tapped, the price chart draws and pings, and the terminal types. Buttons respond to hover.

The ad has a pause button. With the system's "Reduce motion" setting on, the page holds still: the ad rests on its end card, the background and tiles stop, and the demo shows still cards. Animations stop while off screen. Each part is reviewed as a recording of the real site before it goes live: the ad, then the background, then the tiles.

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
| Bundle ID and uw version | 8/10 | `com.uwbrowser.uw`, and "Version 0.1.0 (Chromium 153.0.8010.53)" on the About page once the release command writes the version. The user agent keeps Chromium's version. |
| Release command | 6/10 | Wraps `sign_chrome.py`. The `uw` command's Node helper needs its own entitlements in Chromium's signing config, and notarizing a bundle this size is slow. |
| Sparkle behind Chromium's relaunch button | 5/10 | A patch hands Sparkle's ready update to `UpgradeDetector`, and Chromium's relaunch has to finish Sparkle's install with tabs restored. |
| Problem reports and consent | 5/10 | First-run Yes/No, one settings switch, Crashpad uploads to Sentry, symbols per release, and PostHog in uw's React pages. |
| Codecs through macOS | 4/10 | Plain AAC through AudioToolbox, H.264 through VideoToolbox only, and WebRTC's H.264 without OpenH264. Several media paths, each checked on real sites. |
| Report an issue | 9/10 | One Help-menu item. |
| Website | 8/10 | Doop design first, then Next.js in `site/` on Vercel. |
| Homebrew | 8/10 | A cask that always installs the latest release. It can't be tried end to end until 0.1.0 is published. |

Overall: 5/10. The codec work and the Sparkle hand-off carry the most risk; the rest uses tools that already exist.
