# Windows port audit

Done on 2026-11-01 against Chromium 153.0.8010.53 and uw at 7fa3c8a. The question: what stops uw from building, running, and shipping on Windows? Nothing was built for Windows. The findings come from reading uw's code and patches, Chromium's source and docs, and vendor docs for signing and updates. The [Windows plan](../plans/windows.md) turns them into decisions and stages.

## Result

uw is Mac-only at every layer, but Chromium already has most of what a Windows release needs.

- A Mac can build Windows x64 uw. A Windows machine is needed once, to package Visual Studio and the Windows SDK, and then to run what was built.
- `gn gen` for Windows fails today: five uw targets start with `assert(is_mac)`, and core targets depend on them. Past that, `.mm` files sit in plain source lists, and about 20 call sites treat file paths as narrow strings.
- Chromium's own Windows installer, `mini_installer.exe`, installs per user without admin rights, registers the browser with Windows, and swaps in an update at the next relaunch. uw would need its own install identity and something that downloads the next installer.
- The large pieces are the terminal (ConPTY), video and audio without FFmpeg's H.264 and AAC (Media Foundation), keys on a keyboard without Cmd, and glass.

## Building

### The build host

| | Windows host | Cross build on the Mac |
| --- | --- | --- |
| Tools | Visual Studio 2026 (18.0 or later) with Desktop development with C++ and MFC/ATL, Windows 11 SDK 10.0.28000.2270, SDK Debugging Tools 10.0.26100.3323 or later | The same tools, packaged once on a Windows machine with `depot_tools/win_toolchain/package_from_installed.py` |
| Machine | x86-64, 100 GB free on NTFS, 16 GB RAM or more | The current Mac |
| Setup | `DEPOT_TOOLS_WIN_TOOLCHAIN=0`, gclient from cmd.exe, `core.autocrlf false` | `target_os = ['win']` in `.gclient`, `DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL` and `GYP_MSVS_HASH_<hash>`, then `target_os = "win"` and `target_cpu = "x64"` in the GN args |
| Limits | Remote builds don't support Windows | No 32-bit builds and no remote builds. Tests and the app run only on Windows |

Sources: `.chromium/src/docs/windows_build_instructions.md` and `.chromium/src/docs/win_cross.md`.

`win_cross.md` says cross builds stub out `.asm` files, so Crashpad cannot report crashes. That note looks out of date: on a non-Windows host the build assembles x64 `.asm` files with `llvm-ml` (`build/toolchain/win/toolchain.gni:297-299`), and Crashpad lists its x64 `.asm` files for every host (`third_party/crashpad/crashpad/util/BUILD.gn:552-556`). A test crash in a cross-built uw would settle it.

A Windows 11 on Arm VM on the Mac can run x64 uw and its tests. Building in it is slow: Chromium's Windows clang is x64-only and runs under emulation.

### Repository tooling

| Where | Mac-only today | Windows needs |
| --- | --- | --- |
| `scripts/chromium.ts:263-292` | `doctor` refuses any host but macOS, then checks Xcode, the macOS SDK, Metal, and APFS | A Windows target check: the packaged toolchain for a cross build, or Visual Studio, the SDK, and NTFS on a Windows host |
| `chromium/args.gn:22`, `scripts/chromium.ts:452, 721-724` | `use_system_xcode`, `use_lld = false`, and `--symbols`' `enable_dsyms` | Leave out `use_lld` and `enable_dsyms` for a Windows target. Both fail `--fail-on-unused-args`. `symbol_level = 1` already makes PDBs |
| `scripts/chromium.ts:325-334` | `.gclient` is sealed by its hash | Allow `target_os = ['win']` |
| `chromium/pins.json` | Exactly `macos_sdk_min` and `macos_vm_ipsw` | A Windows SDK minimum |
| `scripts/chromium.ts:132`, `package.json` `kill` and `launch` | `uw.app/Contents/MacOS/uw`, `open`, `pgrep`, `~/Library/Application Support/uw-dev` | `out\uw\chrome.exe`; the name is fixed in `chrome/BUILD.gn`. A Windows VM to run it |
| `scripts/chromium.ts:464-467` | `smoke` reads `--version` | Chromium handles `--version` only on POSIX (`chrome/app/chrome_main_delegate.cc:1181`); read the exe's version resource |
| `scripts/chromium.ts:500-637` | `window-tests` runs in a Tart macOS VM | A Windows VM with a signed-in desktop |
| `scripts/icon.ts` | `actool` makes `app.icns` and `Assets.car` | `.ico` files and Start menu tiles under `chrome/app/theme/chromium/win/` |
| `scripts/release.ts` | `codesign`, `sign_chrome.py`, `notarytool`, the DMG, Sparkle, `plutil`, dSYMs, `shasum` | Authenticode signing, `mini_installer`, a Windows feed, PDB upload. Chromium has no Windows signing script |
| Patches | No `.gitattributes` | `*.patch -text`, or Git for Windows' CRLF conversion breaks `git apply` |

On a Windows host, every depot_tools call in `scripts/chromium.ts` (lines 358, 413, 420, 428, 453, 454, 492) would also need the `.bat` launchers. A cross build keeps the bash ones.

Already portable: `scripts/patches.ts`, the git and checkout parts of `scripts/chromium.ts`, `scripts/google-check.ts` apart from the binary path, and `scripts/release.ts`'s git, `gh`, and `.env` parts.

### The `uw` command

The command is built only into the Mac app bundle (`uw-branding.patch`, in `chrome/BUILD.gn`'s `is_mac` branch). It finds uw through the bundle's layout and the `SingletonLock` symlink, talks over a Unix socket (`browser/uw_command/command_socket.cc`, using `net::UnixDomainServerSocket`), and installs by linking `/usr/local/bin/uw` with an admin prompt (`command_install.mm`).

On Windows: a named pipe limited to the user, a `uw.exe` beside `chrome.exe` and listed in `chrome/installer/mini_installer/chrome.release`, and an install that adds that folder to the user's `PATH` without admin rights. The CLI itself, `command/src/` apart from `launch.ts` and the socket path, is plain Node.

## uw's own C++

| Subsystem | Compiles on Windows? | Works? | Windows counterpart | Size |
| --- | --- | --- | --- | --- |
| GN graph | No: `assert(is_mac)` in `browser/{branding,update,problem_reports,shortcuts}` and `browser/ui/webui/uw_shortcuts`; ungated `.mm` files and `AppKit.framework` | | Platform splits | Small |
| Paths and environment | No: about 20 sites treat `FilePath::value()` and `EnvironmentMap` as narrow strings | | `AsUTF8Unsafe` and `FromUTF8Unsafe` | Small |
| Terminal (`components/terminal`, `browser/terminal`) | No: `posix_openpt`, `TIOCPTYGNAME`, `fork`, `proc_pidinfo`, `/bin/zsh` | No | ConPTY with `CreateProcessW`. The prompt, folder, and running program need shell integration (OSC 133 and OSC 9;9) and a process-tree walk. Chromium has no ConPTY wrapper | Large |
| Connectors and scheduled triggers | No: `pipe`, process groups, `kill`, `/bin/sh` | No | `base::LaunchOptions` handles and a Job Object | Medium |
| AI provider keys | Product yes, tests no | Keys and sign-in tokens last only until quit | `os_crypt_async::OSCryptAsync` (DPAPI on Windows) or Credential Manager | Small |
| Codecs | Yes | No: AAC-LC, HE-AAC, and H.264 without a GPU don't play | Media Foundation; see [Codecs](#codecs) | Large |
| Updates | No | No | See [Installer and updates](#installer-and-updates) | Medium |
| Branding | No | No | `.ico` files, uw's install identity, release version from a GN flag | Medium |
| Problem reports | No | No | `GetUploadUrl` and consent in `chrome/app/chrome_crash_reporter_client_win.cc`, release keys from a GN flag | Small to medium |
| Shortcuts | No | No | `chrome/browser/ui/accelerator_table.cc`, `BrowserView` accelerators, the app menu. Ctrl+letter belongs to programs in a terminal | Medium to large |
| Glass | Link fails: `glass.h` functions exist only in `glass_mac.mm` | No | A stub first. DWM Mica or Acrylic later, which needs a translucent frame | Small stub, large parity |
| Window buttons and floating sidebar | No: `IDS_ENTER_FULLSCREEN_MAC`, mac-only calls | No | Chromium's caption buttons on the right; the floating card needs a Windows design | Small to medium |
| Modifier keys and mouse actions | No: `modifier_keys.mm`, `mouse_actions.mm` | | `views::EventMonitor`, `ui::MouseEvent::GetClickCount()`. Done in T351 with the monitor and `WM_ACTIVATEAPP`; the click count misses a double-click below the tabs, so its message says instead | Small |
| Mini player | Link fails | Floats, but the dock-or-float choice doesn't work | Foreground tracking with `WM_ACTIVATEAPP`, cover from `EnumWindows` and DWM bounds | Medium |
| Workspaces | No: `workspace_window_mac.mm`, `workspace_swipe_mac.mm` | The swipe never fires | `SetWindowPos` and DWM cloaking for hidden partners; DirectManipulation or wheel events for the swipe | Medium |
| Command bar page | No: CoreFoundation in `.cc` files | | `base::GetHourClockType()` and ICU | Small |
| Chat notifications | Yes | Partly | Toasts with uw's own activator CLSID and a Start menu shortcut, both from the installer | Small |

Portable apart from what they link against: `browser/pins`, `browser/privacy`, the page colors, pins, terminal, theme picker, watch card, and command bar views, and `components/{agent,ai_providers,tab_organization,look,uw_command}`. Text that says "this Mac", "macOS Keychain", Cmd, or Option needs Windows wording, including the WebUI's `metaKey` checks.

Tests: 16 browser tests are `.mm` files, some tests depend on mac-only Chromium targets, and the terminal and `uw` command tests are POSIX-only.

## Chromium patches

| Patch | Windows twin | Size |
| --- | --- | --- |
| `uw-branding` | `chrome/install_static/chromium_install_modes.h` (install folder, registry key, AppUserModelID, ProgID, toast activator and other CLSIDs), `chrome/installer/mini_installer/mini_installer_constants.cc` (`Software\Chromium`), `chrome/app/theme/chromium/win/` icons, 61 Windows-only "Chromium" strings in `chromium_strings.grd` | Medium |
| `tab-tree`, `uw-terminal`, `uw-shortcuts` | `chrome/browser/ui/accelerator_table.cc`, `app_menu_model.cc`, `tab_menu_model.cc`, `BrowserView::LoadAccelerators`. Ctrl+S, Ctrl+1 to 9, Ctrl+Shift+L, and Ctrl+Shift+M are taken, and Ctrl+Alt is AltGr on many layouts | Medium to large |
| `uw-look` | `browser_frame_view_win.cc`, `desktop_window_tree_host_win.cc`, `hwnd_message_handler.cc`. Chromium's Windows frame is opaque on purpose | Large |
| `uw-workspaces` | Links from other apps arrive through `startup_browser_creator.cc`. Windows gives each profile its own taskbar group (`shell_integration_win.cc`), and workspaces are profiles | Medium |
| `uw-distribution` | `chrome_crash_reporter_client_win.cc` (built into `chrome_elf`), `crashpad_win.cc`, and the metrics manager, which turns crash uploads off on Windows (`chrome_metrics_services_manager_client.cc:374-395`). Help → Report an Issue goes in `HelpMenuModel` | Large |
| `uw-codecs` | `media/filters/win/media_foundation_audio_decoder.cc`, `gpu_mojo_media_client_win.cc`, `default_decoder_factory.cc`, `webrtc_util.cc` | Large |
| `agent-chats` | `NotificationPlatformBridgeWin::DisplayServiceShutDown` is empty, so Windows already keeps every toast after quit, sites' too | Small |
| `uw-busy-terminals` | The `#else` branch of `BrowserCloseManager::CheckForDownloadsInProgress` | Small |
| The other 14 | Apply to every platform | None |

A Windows install identity, like the Mac's bundle ID, must not change after people install uw: Windows keys the install folder, registry, taskbar pins, default-browser choice, and notifications on it.

## Distribution

### Installer and updates

Unbranded Chromium builds a working `mini_installer.exe` (`chrome/installer/mini_installer/BUILD.gn`). It installs per user to `%LOCALAPPDATA%\<product>\Application\` with no admin prompt and no window, adds the Start menu shortcut and uninstall entry, registers the browser and ProgIDs, and registers `notification_helper.exe` for toasts. Run while the browser is open, `setup.exe` leaves `new_chrome.exe`, and Chromium swaps it in at the next quit or launch (`chrome/browser/first_run/upgrade_util_win.cc`). Each release needs a higher four-part `chrome/VERSION`: setup treats only a higher version as an update.

Chromium's "Relaunch to update" chip watches the installed version in the registry (`installed_version_poller.cc`), but only with `enable_update_notifications = true`, which is off for unbranded builds (`chrome/browser/buildflags.gni:20`).

So a Windows updater only has to check a feed, download the signed installer, check its signature, and run it silently. Options:

| Option | Fit |
| --- | --- |
| [WinSparkle](https://winsparkle.org/guides/publishing-updates/) 0.9.4 | Reads Sparkle's appcast format with EdDSA signatures. It always shows its own "update available" window; a callback lets the app take the downloaded file instead |
| A small fetcher of uw's own | No new library, but new code to check and verify |
| [Velopack](https://docs.velopack.io/) 1.2 | Its own installer and feed, replacing `mini_installer` and the browser registration it does |
| MSIX with an App Installer file | The install folder is read-only, so no own updater. Default-browser registration works, as Firefox's MSIX shows |
| Chromium's updater (`chrome/updater`) | Needs an Omaha server that signs each answer. Too heavy for one person |

### Signing

- [Azure Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart), formerly Trusted Signing: individuals only in the US and Canada. $9.99 a month for 5,000 signatures. Microsoft holds the key and issues a three-day certificate daily, so every signature needs a timestamp. It signs from a Mac with [jsign](https://ebourg.github.io/jsign/).
- Elsewhere, an individual-validated certificate from a CA: [SSL.com IV](https://www.ssl.com/products/software-integrity/code-signing/iv/) at $129 a year plus cloud signing from $15 a month, or Certum from about $249. Keys must live in hardware or a cloud HSM since 2023, and certificates issued since 2026-03-01 last at most 460 days.
- [SignPath Foundation](https://signpath.org/terms) needs an OSI license, which uw doesn't have.
- [SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation) warns about new apps for "several weeks and hundreds of clean installs" whatever the certificate. EV certificates stopped skipping this in 2024.

### Package managers

- [winget](https://learn.microsoft.com/en-us/windows/package-manager/package/repository): one pull request to `microsoft/winget-pkgs` per release, reviewed by a person. The installer must install silently, and GitHub release URLs are fine. `RequireExplicitUpgrade` keeps a self-updating app out of `winget upgrade --all`.
- Scoop: a bucket of its own, like the Homebrew tap. Scoop's own folder layout would likely fight uw's updater.

### Crash symbols

`sentry-cli debug-files upload` takes the `.exe`, `.dll`, and `.pdb` files. Sentry's limit is 4 GB per file, so release builds keep `symbol_level = 1`.

### Codecs

The Mac plays H.264 and AAC through macOS's decoders so that uw ships no FFmpeg H.264 or AAC ([Codecs](../plans/distribution.md#codecs)). On Windows the same rule means Media Foundation:

- AAC: `MediaFoundationAudioDecoder` exists, but uw's Windows build would accept only xHE-AAC on Windows 11 22H2 and later (`media_foundation_audio_decoder.cc:102-109`, `gpu_mojo_media_client_win.cc:104-107`). AAC-LC and HE-AAC need both changed, and Web Audio's `decodeAudioData` has no Media Foundation path in the page's process.
- H.264: only hardware decoding through `D3D11VideoDecoder`. Nothing plays it with the GPU off, in many VMs, or on blocklisted GPUs, until uw wraps Media Foundation's software decoder.
- WebRTC sends H.264 only through hardware encoders.
- Windows "N" editions ship without these codecs.

## Found on the way: the Mac's relaunch chip

`enable_update_notifications` is off in uw's Mac build too (`out/uw/gen/chrome/browser/buildflags.h:12`), and `UpgradeDetectorImpl` listens to `BuildState` only when it is on (`upgrade_detector_impl.cc:483-494`). So Sparkle's `BuildState::SetUpdate` (`browser/update/sparkle_updater.mm:87`) likely reaches nothing, and "Relaunch to update" may never appear. `update_browsertest.cc` checks only `BuildState`. Recorded on T218.
