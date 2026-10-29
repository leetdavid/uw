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

Decided on 2026-10-27:

- Raw `.aac` files and ADTS streams keep playing. uw reads ADTS itself and hands the audio to AudioToolbox (T229).
- WebRTC keeps sending H.264, through VideoToolbox's encoder.
- When accelerated video decoding is off, as with `--disable-gpu`, or the hardware turns a stream down, H.264 plays through VideoToolbox's software decoder in the GPU process, which every launch on a Mac has, with frames copied for the page (T230). Changed the same day from "in another process" once research found the GPU process is always there.
- Web Audio's `decodeAudioData` decodes AAC with AudioToolbox in the page's own process. Its sandbox allows the two things AudioToolbox needs, the same two the GPU process has: the audio component registrar and the `AudioCodecs.component` bundle. Chosen by the user over decoding in the GPU process.

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

Decided on 2026-10-27:

- All of uw's own pages report problems, React and Lit alike. A small detector in each page uses PostHog's definitions of rage and dead clicks and sends what it finds over Mojo. It sends an element's tag, role, and label, never its text.
- PostHog tells installs apart by a random ID that uw makes on Yes and deletes on No.
- The question and the switch follow the signed-off frames "Problem reports: asked on first run" and "Problem reports: the settings switch".
- Crashes go to the Sentry project `eslee/uw` (US). `pnpm run release` writes its minidump address and the PostHog key into the bundle's `Info.plist`, as it does Sparkle's feed. Daily builds carry neither, never ask, and send nothing.
- `pnpm run release` uploads the dSYMs with a Sentry token from the keychain item `uw-sentry`, and Sentry names each release `uw@<version>`.

## Build flags

Agreed: previews use the same `chromium/args.gn` as daily builds, plus `symbol_level = 1` and `enable_dsyms = true` for crash reports, so what people download matches what the owner uses. An official build with link-time and profile-guided optimization may come later, once measured.

Decided on 2026-10-29: Chromium's dump-only checks (`DUMP_WILL_BE_CHECK`, `DUMP_WILL_BE_NOTREACHED`, and `CHECK`s that are not fatal until a later milestone) log and send a crash report without crashing, as they do in Chrome, though uw's builds are not official builds. Outside official builds Chromium makes them fatal, and 0.1.0 crashed at every start on a profile an earlier build had left, in such a check about search engine data. `uw-checks.patch` changes `GetDumpSeverity` in `base/check.cc` (T247).

## Website content

Agreed: one calm page that sells uw, "dawn at night". The approved design is frame `XS-shfv_9t` on the Doop canvas, chosen on 2026-10-28 over a louder poster direction because uw should feel zen. It shows only uw's own interface, never real or made-up websites, and only features that are built.

1. Hero: a night sky. "ur web, ur way" in Instrument Serif, with "ur way" in a pink-to-cyan italic. One line on what uw is, then "Download for Mac" beside the [Homebrew](#homebrew) command, and "Apple silicon · macOS 13 or later". Below, a uw window rises from a glowing horizon: tasks in the sidebar, a video docked at its foot, and the agent asking before it closes tabs. As the page scrolls, it [becomes the film](#the-window-becomes-the-film).
2. How it works: the four stops as a filmstrip of small sidebars: the Scratchpad, tabs filed into tasks, a new task being named, and Undo.
3. Features: glass tiles for the mini player docked in the sidebar, the command bar, the agent, workspaces and themes, watching a page, the terminal, and privacy: the user's own AI provider, and nothing reported without a yes.
4. A closing section that says the preview is rough, with the download button and the Homebrew command over a second horizon. Then a footer linking releases, GitHub Issues, and the source.

Motion, not built yet: the stars and the horizon glow breathe slowly, the filmstrip lights each stop in turn, sections fade and rise in as they scroll into view, and each tile plays a short loop of its feature while on screen. Buttons respond to hover. With the system's "Reduce motion" setting on, the page holds still, and animations stop while off screen. Each part is reviewed as a recording of the real site before it goes live.

### The window becomes the film

Agreed on 2026-10-29: the hero window turns into the ad as the page scrolls. The storyboard is Doop frame `d7LslD48S1`. The film is take 3 of the ad: 39.6 s at 1920 × 1080, with music and sound effects, rendered from the HyperFrames project in `~/dev/uw-ad`, outside this repo. It remakes the animated HTML ad from commit `8dd05ed`.

1. The window rises with the page and turns to face the viewer, as it does now.
2. Once it is flat and centered, it pins in place with its horizon while the page keeps scrolling.
3. While pinned, its sidebar and chat blur away into the film's first frame, and the frame changes from the window's shape to the film's 16:9.
4. The film then starts from the beginning, muted, and plays once at normal speed. It does not loop, and scrolling does not move it forward or back.
5. After about one more screen the page lets go. The film keeps playing as it scrolls away and pauses once it is mostly out of view.

The page stops softly at the film: scrolling slowly into the pinned stretch settles where the film is fully formed, and a quick flick goes past. Nothing holds the page while the film plays.

Two small round buttons in dark glass sit in the film's bottom-right corner while it shows: pause and play, and sound on and off. Turning sound on carries on from where the film is.

The film stops on its end card, and the pause button becomes Replay. A real download link sits exactly over the end card's drawn "Download for Mac", placed as a share of the film's size so it lines up at any width.

Open: phone widths, and "Reduce motion" and browsers without scroll-driven animation.

No email list or pricing yet. Help is its own plan: [Help](help.md).

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
| Problem reports and consent | 5/10 | First-run Yes/No, one settings switch, Crashpad uploads to Sentry, symbols per release, and every uw page's problems sent to PostHog through uw. |
| Codecs through macOS | 4/10 | Plain AAC through AudioToolbox, H.264 through VideoToolbox only, and WebRTC's H.264 without OpenH264. Several media paths, each checked on real sites. |
| Raw AAC without FFmpeg | 7/10 | An early ADTS check, then a uw engine in Chromium's HLS player that feeds its ADTS parser. |
| H.264 without the GPU process | 5/10 | VideoToolbox's software decoder in the GPU process with frames copied for the page, chosen when accelerated decoding is off or refuses a stream. |
| Report an issue | 9/10 | One Help-menu item. |
| Website | 8/10 | Doop design first, then Next.js in `site/` on Vercel. |
| Homebrew | 8/10 | A cask that always installs the latest release. It can't be tried end to end until 0.1.0 is published. |

Overall: 5/10. The codec work and the Sparkle hand-off carry the most risk; the rest uses tools that already exist.
