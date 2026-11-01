# Windows

Agreed on 2026-11-01. Signing is decided after the first Windows preview. Stage 1 starts now; later stages wait for their own go-ahead. [Product](../product.md#minimum-usable-version) targets macOS first; this plan says what a Windows version of uw would take and which decisions come first. The findings and sources are in the [Windows port audit](../research/windows-port-audit.md).

Implementability: **3/10** for the whole plan and **4/10** for the first preview, stages 1 to 4, using the [roadmap convention](roadmap.md#implementability-estimates). Each stage has its own score. Chromium already builds, installs, and updates on Windows, and much of uw's code is portable. The terminal, the codecs, the keys, and glass each need Windows code of their own, and every later feature has to be built and checked twice.

## Agreed

- When, agreed on 2026-11-01: stage 1 starts now. Later stages wait for their own go-ahead. Stage 1 was done the same day, as T286 to T288.
- Build host, agreed on 2026-11-01: the owner's Linux workstation, `workstation` on Tailscale, cross-builds Windows uw in a Chromium checkout of its own. Linux to Windows is the cross build Chromium's own CI tests. A Windows 11 x64 VM on the same machine packages Visual Studio and the Windows SDK once, and runs uw and its tests at full speed. The VM runs in Docker with [dockur/windows](https://github.com/dockur/windows), so the workstation needs no `sudo` and no system packages, and it uses Microsoft's free 90-day Windows 11 Enterprise evaluation. When the evaluation runs out, the VM is installed again from the same setup. The Mac and the home server were turned down: the Mac is short of disk and would test under Arm emulation, and the server has 31 GB of RAM.
- Processors, agreed on 2026-11-01: x64 only, as the Mac is Apple silicon only. Windows on Arm runs it under emulation.
- Installer, agreed on 2026-11-01: Chromium's `mini_installer`. Chrome ships on it, so it needs the least upkeep: uw changes only names, IDs, and icons. It installs per user without an admin prompt, registers uw as a browser, and swaps in an update at relaunch.
- Updates, agreed on 2026-11-01: a small fetcher of uw's own. Once a day it reads a feed on the latest GitHub release, downloads the installer, checks its signature, and runs it silently. Chromium's "Relaunch to update" then appears, as on the Mac, with `enable_update_notifications` on. WinSparkle was turned down: it always shows its own window when it finds an update, and the Mac's updater windows never show. See [ADR 0026](../adr/0026-windows-uw-updates-with-a-fetcher-of-its-own-behind-chromiums-relaunch-button.md).
- Codecs, agreed on 2026-11-01: the Mac's [codecs rule](distribution.md#codecs) holds. Windows uw plays H.264 and AAC through Media Foundation, Windows' own licensed decoders, and ships no FFmpeg H.264 or AAC. Windows "N" editions need Microsoft's Media Feature Pack.
- First preview, agreed on 2026-11-01: it leaves out glass, the workspace swipe, and the terminal tab, which take the most Windows-only code. The `uw` command ships in it. The three follow in stage 5.
- Keyboard shortcuts, agreed on 2026-11-01: uw follows Arc for Windows. Cmd becomes Ctrl and Option becomes Alt. Ctrl+S hides the sidebar, Ctrl+Alt+← and → and Alt+1 to 9 switch workspaces, and Ctrl+1 to 9 still go to a tab. Shortcuts Arc doesn't have are drawn on the Doop canvas and signed off. Which shortcuts a terminal tab hands to uw is decided with stage 5.
- Install identity, agreed on 2026-11-01: uw sits under a company folder, as Chrome sits under `Google\Chrome`: `%LOCALAPPDATA%\uwbrowser\uw\Application` for the app, `%LOCALAPPDATA%\uwbrowser\uw\User Data` for profiles, and `HKCU\Software\uwbrowser\uw` in the registry. AppUserModelID `uwbrowser.uw`, ProgID prefix `uwHTML`, launch scheme `uwbrowser`, and CLSIDs of its own. Like the bundle ID, none of it can change once people install uw.

## Distribution

Agreed on 2026-11-01. People download Windows uw from the website, and it updates itself.

- Windows 11 only. The installer refuses Windows 10, and the website says so.
- One release carries both platforms. `pnpm run release` on the Mac builds the Mac app, has the workstation build Windows, and puts `uw-windows-x64-setup.exe` beside the DMG on the same GitHub release, so the website's "latest" links find both.
- Each Windows release has a higher four-part version than the last, as Chromium's installer requires: Chromium's version with its last number times 100 plus a count, so uw's releases on Chromium 153.0.8010.53 are 153.0.8010.5300, 5301, and so on. About shows uw's own version, as on the Mac. See [ADR 0027](../adr/0027-windows-uw-numbers-its-releases-from-chromiums-version.md).
- Updates come from a small feed file on each release, signed on the Mac with the key that signs Sparkle's feed. The key never leaves the Mac. Proposed: the feed names the version, the installer's address and size, and its signature.
- The first Windows preview is not signed. Windows warns on its first run ("Windows protected your PC", then More info and Run anyway), and the website and Help say so. Windows 11's Smart App Control blocks it outright. Updates download in uw, so the warning does not come back. Signing is decided later.
- The website's button matches the visitor's system: "Download for Windows" with "Windows 11, 64-bit" under it on Windows, the Mac button elsewhere, and a small link to the other. Drawn on the Doop canvas and signed off first.
- winget from the first preview: `winget install uwbrowser.uw`. `pnpm run release` opens the pull request to Microsoft's `winget-pkgs` for each release. The manifest marks uw as updating itself, so `winget upgrade --all` leaves it alone.
- Help follows the reader's system: keys and steps show for Mac or Windows, with a switch at the top of Help. Pages are written once, with a component for keys. uw's agent, which reads Help through its connector, gets both.

## Stages

| Stage | Score | What it takes |
| --- | --- | --- |
| 1. Builds for Windows | 6/10 | uw's build commands run on Linux and cross-build Windows uw on the workstation. Platform splits for the five `assert(is_mac)` targets and the `.mm` files, wide-string path fixes, and stubs for glass, window buttons, the swipe, the terminal, and the `uw` command. Done when `chrome`, `browser_tests`, and `mini_installer` build on the workstation, and the installer installs uw in the Windows VM |
| 2. Runs on Windows | 5/10 | uw's install identity, icons, and strings. Windows' minimize, maximize, and close buttons, which uw's layout leaves out today, and no "Google API keys are missing" bar; both seen on the first run in the VM on 2026-11-01. Provider keys and sign-in tokens saved with `OSCryptAsync`. Arc's Windows shortcuts and menu items. Crash reports to Sentry. The `uw` command over a named pipe, packaged as `uw.exe` and put on the user's `PATH` without an admin prompt (6/10). Done when the installer from stage 1 installs uw in the VM, and uw's browser tests pass there |
| 3. Plays video | 3/10 | AAC and H.264 through Media Foundation, with a software decoder for H.264, and WebRTC's H.264. Done when the sites the Mac checks play in the VM, with the GPU on and off |
| 4. Ships | 5/10 | A Windows path in `pnpm run release`, the update fetcher and its signed feed, the Windows 10 check, the website's download button, Help that follows the reader's system, and the winget package, per [Distribution](#distribution). Done when a preview installs from the website, and updates itself to the next |
| 5. Matches the Mac | 3/10 | The terminal through ConPTY, the mini player's dock-or-float choice, hidden workspace windows and the swipe, and glass through Mica |

## Testing

Tests run in the Windows 11 VM on the workstation, as the Mac's real-window tests run in Tart, so nothing opens on the owner's screen. `pnpm run google-check` runs on Windows too before a release.

## Open questions

- Whether a cross-built uw reports crashes. `win_cross.md` says no; the build files suggest yes.
