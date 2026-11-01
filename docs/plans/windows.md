# Windows

Proposed on 2026-11-01, and being agreed one decision at a time. [Product](../product.md#minimum-usable-version) targets macOS first; this plan says what a Windows version of uw would take and which decisions come first. The findings and sources are in the [Windows port audit](../research/windows-port-audit.md).

Implementability: **3/10** for the whole plan, using the [roadmap convention](roadmap.md#implementability-estimates). Each stage has its own score. Chromium already builds, installs, and updates on Windows, and much of uw's code is portable. The terminal, the codecs, the keys, and glass each need Windows code of their own, and every later feature has to be built and checked twice.

## Agreed

- When, agreed on 2026-11-01: stage 1 starts now. Later stages wait for their own go-ahead.
- Build host, agreed on 2026-11-01: the owner's Linux workstation, `workstation` on Tailscale, cross-builds Windows uw in a Chromium checkout of its own. Linux to Windows is the cross build Chromium's own CI tests. A Windows 11 x64 VM under KVM on the same machine packages Visual Studio and the Windows SDK once, and runs uw and its tests at full speed. The Mac and the home server were turned down: the Mac is short of disk and would test under Arm emulation, and the server has 31 GB of RAM.
- Processors, agreed on 2026-11-01: x64 only, as the Mac is Apple silicon only. Windows on Arm runs it under emulation.
- Installer, agreed on 2026-11-01: Chromium's `mini_installer`. Chrome ships on it, so it needs the least upkeep: uw changes only names, IDs, and icons. It installs per user without an admin prompt, registers uw as a browser, and swaps in an update at relaunch.
- Updates, agreed on 2026-11-01: a small fetcher of uw's own. Once a day it reads a feed on the latest GitHub release, downloads the installer, checks its signature, and runs it silently. Chromium's "Relaunch to update" then appears, as on the Mac, with `enable_update_notifications` on. WinSparkle was turned down: it always shows its own window when it finds an update, and the Mac's updater windows never show ([ADR 0019](../adr/0019-sparkle-updates-behind-chromiums-relaunch-button.md)).
- Signing, agreed on 2026-11-01: none until stage 4. Stages 1 to 3 run only in the owner's VM.
- Codecs, agreed on 2026-11-01: the Mac's [codecs rule](distribution.md#codecs) holds. Windows uw plays H.264 and AAC through Media Foundation, Windows' own licensed decoders, and ships no FFmpeg H.264 or AAC. Windows "N" editions need Microsoft's Media Feature Pack.

## Open decisions

Each has a proposed default.

| Decision | Options | Proposed |
| --- | --- | --- |
| Signing, by stage 4 | Azure Artifact Signing, a CA certificate, or unsigned | Artifact Signing if the owner lives in the US or Canada; an individual-validated certificate otherwise |
| Keys | A Windows key list | Designed on the Doop canvas. Ctrl+letter belongs to programs in a terminal tab, so the Mac's "must include Cmd" rule can't carry over |
| First preview's features | Everything, or a smaller set | Leave out glass, the swipe, and the terminal at first. Those three take the most Windows-only code |
| Install identity | Folder, registry key, AppUserModelID, ProgID, and CLSIDs | `uw` for the folder and registry key, AppUserModelID `uwbrowser.uw`, and new CLSIDs. Like the bundle ID, it can't change after people install |

## Stages

| Stage | Score | What it takes |
| --- | --- | --- |
| 1. Builds for Windows | 6/10 | uw's build commands run on Linux and cross-build Windows uw on the workstation. Platform splits for the five `assert(is_mac)` targets and the `.mm` files, wide-string path fixes, and stubs for glass, window buttons, the swipe, the terminal, and the `uw` command. Done when `chrome`, `browser_tests`, and `mini_installer` build on the workstation, and the installer installs uw in the Windows VM |
| 2. Runs on Windows | 5/10 | uw's install identity, icons, and strings. Keys saved with `OSCryptAsync`. Windows keys and menu items. Crash reports to Sentry. Done when the installer from stage 1 installs uw in the VM, and uw's browser tests pass there |
| 3. Plays video | 3/10 | AAC and H.264 through Media Foundation, with a software decoder for H.264, and WebRTC's H.264. Done when the sites the Mac checks play in the VM, with the GPU on and off |
| 4. Ships | 5/10 | Signing, a Windows path in `pnpm run release`, updates, the website's download button, Help pages with Windows keys, and a winget package. Done when a preview installs from the website, and updates itself to the next |
| 5. Matches the Mac | 3/10 | The terminal through ConPTY, the `uw` command over a named pipe, the mini player's dock-or-float choice, hidden workspace windows and the swipe, and glass through Mica |

## Testing

Tests run in the Windows 11 VM on the workstation, as the Mac's real-window tests run in Tart, so nothing opens on the owner's screen. `pnpm run google-check` runs on Windows too before a release.

## Open questions

- Whether a cross-built uw reports crashes. `win_cross.md` says no; the build files suggest yes.
