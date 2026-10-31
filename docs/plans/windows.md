# Windows

Proposed on 2026-11-01. Nothing here is agreed. [Product](../product.md#minimum-usable-version) targets macOS first; this plan says what a Windows version of uw would take and which decisions come first. The findings and sources are in the [Windows port audit](../research/windows-port-audit.md).

Implementability: **3/10** for the whole plan, using the [roadmap convention](roadmap.md#implementability-estimates). Each stage has its own score. Chromium already builds, installs, and updates on Windows, and much of uw's code is portable. The terminal, the codecs, the keys, and glass each need Windows code of their own, and every later feature has to be built and checked twice.

## Decisions

Each has a proposed default. None is agreed.

| Decision | Options | Proposed |
| --- | --- | --- |
| When | Now, after the Mac previews settle, or not planned | The owner's call. Stage 1 is cheap and keeps new code from adding Mac-only parts |
| Build host | Cross build on the Mac, a Windows PC, or a cloud VM | Cross build on the Mac, in its own output folder. A Windows 11 VM on the Mac packages the toolchain once and runs uw |
| Processors | x64, Arm64, or both | x64 only, as the Mac is Apple silicon only. Arm64 Windows runs it under emulation |
| Signing | Azure Artifact Signing, a CA certificate, or unsigned | Artifact Signing if the owner lives in the US or Canada; an individual-validated certificate otherwise |
| Installer | Chromium's `mini_installer`, Velopack, Inno Setup, or MSIX | `mini_installer`: it already installs per user, registers the browser, and swaps in updates at relaunch |
| Updates | WinSparkle, a small fetcher of uw's own, or none | WinSparkle, if its window can stay hidden, as Sparkle's does on the Mac; a fetcher of uw's own otherwise |
| Codecs | Media Foundation, or FFmpeg's H.264 and AAC | Media Foundation, so the Mac's [codecs rule](distribution.md#codecs) holds |
| Keys | A Windows key list | Designed on the Doop canvas. Ctrl+letter belongs to programs in a terminal tab, so the Mac's "must include Cmd" rule can't carry over |
| First preview's features | Everything, or a smaller set | Leave out glass, the swipe, and the terminal at first. Those three take the most Windows-only code |
| Install identity | Folder, registry key, AppUserModelID, ProgID, and CLSIDs | `uw` for the folder and registry key, AppUserModelID `uwbrowser.uw`, and new CLSIDs. Like the bundle ID, it can't change after people install |

## Stages

| Stage | Score | What it takes |
| --- | --- | --- |
| 1. Builds for Windows | 6/10 | A `--target win` cross build in `out/uw-win`. Platform splits for the five `assert(is_mac)` targets and the `.mm` files, wide-string path fixes, and stubs for glass, window buttons, the swipe, the terminal, and the `uw` command. Done when `chrome`, `browser_tests`, and `mini_installer` build on the Mac |
| 2. Runs on Windows | 5/10 | uw's install identity, icons, and strings. Keys saved with `OSCryptAsync`. Windows keys and menu items. Crash reports to Sentry. Done when the installer from stage 1 installs uw in the VM, and uw's browser tests pass there |
| 3. Plays video | 3/10 | AAC and H.264 through Media Foundation, with a software decoder for H.264, and WebRTC's H.264. Done when the sites the Mac checks play in the VM, with the GPU on and off |
| 4. Ships | 5/10 | Signing, a Windows path in `pnpm run release`, updates, the website's download button, Help pages with Windows keys, and a winget package. Done when a preview installs from the website, and updates itself to the next |
| 5. Matches the Mac | 3/10 | The terminal through ConPTY, the `uw` command over a named pipe, the mini player's dock-or-float choice, hidden workspace windows and the swipe, and glass through Mica |

## Testing

Tests run in a Windows 11 VM on the Mac, as the Mac's real-window tests run in Tart, so nothing opens on the owner's screen. Tart can't run Windows, so the VM needs Parallels or another host. `pnpm run google-check` runs on Windows too before a release.

## Open questions

- Whether a cross-built uw reports crashes. `win_cross.md` says no; the build files suggest yes.
- Whether WinSparkle can check and download without showing its window.
- How much of a Windows 11 VM's x64 emulation the tests can take.
