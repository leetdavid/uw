<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo-dark.svg">
    <img src="docs/assets/logo-light.svg" width="160" height="160" alt="uw browser logo">
  </picture>
</p>

<h1 align="center">uw browser</h1>

<p align="center"><strong>ur web, ur way</strong></p>

uw browser is a Chromium-based browser for macOS in early development. Its main features are [automatic tab organization](docs/product.md#automatic-tab-organization), [agent automation](docs/product.md#agent-automation), and [integrations](docs/product.md#integrations) such as a terminal tab. [Verification status](#verification-status) records what has been built and tested.

[Product docs and roadmap](docs/README.md)

## Build-host requirements

- macOS on Apple Silicon or Intel.
- Node.js 24.13+; the CI version is pinned in [`.node-version`](.node-version).
- [pnpm](https://pnpm.io/installation), at the version pinned in [`package.json`](package.json).
- Git and working `curl` on PATH.
- Full Xcode, macOS SDK 26.5 or newer, and the Metal Toolchain.
- An APFS build volume and a checkout path without spaces.
- 100 GiB free for a fresh checkout, or 50 GiB free for an existing checkout's sync/build.

The disk thresholds are project preflight checks.

The browser core is C++; repository tooling is TypeScript. Chromium's upstream tools require `python3` on PATH, then `depot_tools` installs its pinned Python runtime.

## Setup

### 1. Finish Xcode setup

Install full Xcode, select it, then complete its license and first-launch setup:

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license
sudo xcodebuild -runFirstLaunch
xcodebuild -downloadComponent MetalToolchain
```

### 2. Install tooling

From the repository root:

```sh
pnpm install --frozen-lockfile --ignore-scripts
```

Sources and build output go in `.chromium/`, which Git ignores. To use another APFS volume, set the checkout path first:

```sh
export UW_CHROMIUM_ROOT=/Volumes/Build/uw-chromium
```

Use a dedicated, empty directory. Keep the same setting for every build command.

### 3. Check the host

```sh
pnpm run doctor
```

`doctor` is read-only. Fix any failures before downloading Chromium.

If `curl` fails because it is a Homebrew-internal shim, remove those shim directories from PATH. macOS provides `/usr/bin/curl`.

## Building

These are the commands used on the verified 64 GiB Mac:

```sh
caffeinate -i pnpm run sync --jobs 8
caffeinate -i pnpm run build --jobs 8
pnpm run smoke
```

- `sync` downloads the pinned Chromium source, dependencies, and upstream tools, then applies uw's patches.
- `build` prepares the current patches, generates the build files, and compiles the `chrome` target into `uw.app`.
- `smoke` checks the uw product name and version, then runs a headless JavaScript/DOM test in a temporary incognito profile, with the sandbox enabled.

Start with `--jobs 2` on a 16 GiB Mac. `caffeinate` prevents idle sleep during the download and build. Rerun `build` to reuse completed work after an interruption.

Sync and build repeat the host checks. Sync accepts the exact recorded uw patches, removes them before updating Chromium, and reapplies the current series afterward. It refuses unrelated directories and additional local edits in Chromium, its tools, or tracked dependencies.

Use `--checkout-dir PATH` instead of `UW_CHROMIUM_ROOT` if preferred. Each command has help:

```sh
pnpm run build --help
```

## Using your local build

After a successful build, launch the application with a separate development profile:

```sh
pnpm run launch
```

`launch` starts a new `uw.app` instance and returns without waiting for it to close. uw gets the environment the Finder would give it, not your terminal's, so a shell in a uw terminal tab does not think it runs inside the terminal you launched from. It also records every organizer request and answer in `~/Library/Application Support/uw-dev/organizer-log.jsonl`; `pnpm run launch:nolog` starts uw without that record. See [Try uw](docs/try-uw.md). It uses `~/Library/Application Support/uw-dev` to keep development settings and extensions separate. For tab restoration, choose "Continue where you left off" at `chrome://settings/onStartup`. Quit this build before rebuilding it. When uw is already running, `launch` asks whether to quit it and start it again. An answer that starts with `y`, such as `y` or `yes`, does that; any other answer leaves uw running and starts nothing, because a second launch would only open a window of the running copy, not the new build. `pnpm run launch -y` quits it and starts it again without asking. `pnpm run kill` quits every running copy of this build.

Launching `uw.app` normally uses `~/Library/Application Support/uw`. Tabs are always vertical. A profile that chose horizontal tabs before opens with vertical tabs, and no menu or setting offers horizontal tabs.

For current manual test scenarios and known limits, see [Try the current build](docs/try-uw.md).

## Customizing Chromium

[`chromium/patches/series`](chromium/patches/series) lists patches in application order:

- `uw-branding.patch` changes the product and About names, macOS bundle identity, and default profile location. It points the app bundle at uw's [app icon](docs/logo.md#app-icon). Chromium's logos inside the browser and most secondary strings remain.
- `vertical-tabs-default.patch` changes the registered tab-orientation default. The pinned revision already enables the native vertical-tabs launch feature.
- `tab-tree.patch` hooks the window-owned tab organization controller into tab opening, session restore, tab drags, and the tab context menu. It hooks the [pins shared by a profile's windows](docs/plans/workspaces.md#how-pins-are-kept) into each window, session restore, Cmd-W, the pin's tile, and its menu. It gives each window its [workspace](docs/CONTEXT.md#language), keeps each window's session data through a rebuild of the session file, and binds the workspace keys. It puts the workspace switcher and name row in the sidebar, New Workspace in the commands, and Move to Workspace in the tab menu, and takes the profile row out of the app menu. It adds the sidebar and menu strings and links the uw tests into Chromium's test binaries. It expects the product source prepared below.
- `ai-organization.patch` registers the automatic organization, pin, workspace, and shared provider preferences.
- `uw-settings-page.patch` adds the uw page and its strings to Chromium's settings.
- `uw-scheme-alias.patch` gives every [uw page](docs/CONTEXT.md#language) a `uw://` address. A `uw://` address opens its `chrome://` page, typed in the address bar or opened by the browser. The address bar shows and copies a `chrome://` address as `uw://`. A web page cannot open them, as with `chrome://`.
- `uw-look.patch` adds the window tint: it hooks uw's colors and gradient into Chromium's frame painting and turns the transparent glass frame off by default.
- `uw-workspaces.patch` brings each window back in its workspace after a restart or Cmd+Shift+T, and keeps a window that shows another workspace hidden until it is switched to. It gives the tab strip a slot for the workspace's name under the pins, hides the profile button, removes the Profiles menu, and never shows the profile picker at launch. See [ADR 0009](docs/adr/0009-switching-workspace-swaps-the-window.md).

Substantial Chromium product code lives in tracked `browser/` and `components/` directories. Preparation links them into the ignored Chromium checkout at `src/uw/`; it does not copy the files. The tab-tree patch ignores that generated source directory. This keeps product source reviewable while preserving an upstream-only checkout between prepare runs.

After editing the tracked patches, prepare an existing synced checkout without downloading or compiling:

```sh
pnpm run prepare:chromium
```

`build` also runs this step. The runner validates the whole series in a temporary Git index before changing source files. It applies with `git apply`, without fuzz or whitespace repair, and leaves the real index untouched. Repeating preparation with the same result is a no-op.

The checkout's `.uw-patches.json` records the applied result and operation status so patch updates and sync can reverse it exactly. Interrupted operations can recover files already patched or removed during replacement. Other source edits, staged changes, and untracked files stop preparation. Preserve exploratory edits as patches before rerunning it; do not delete that record to bypass the check. The runner applies patches only to tracked Chromium files, then links tracked product source beneath `src/uw/`. New modules follow the [source-organization guidance](docs/architecture.md#code-organization).

## Tooling checks

```sh
pnpm run check
```

This runs strict TypeScript checking and the Node.js test suite. These checks do not require Python or a Chromium checkout. Browser compilation and smoke testing are separate checks.

```sh
pnpm run check && pnpm run verify --jobs 8
```

`verify` builds, runs the smoke test, then runs the focused `unit_tests` and `browser_tests` suites. It defaults to `--jobs 2`, which suits a 16 GiB Mac.

The browser tests run in Chromium's headless mode, so they open no windows and no Dock icon. To watch one, run `.chromium/src/out/uw/browser_tests --gtest_filter='<suite>.*' --headful-mode`.

To see how the window looks without opening it, set `UW_SCREENSHOT_DIR` to an existing folder before running `verify` or the browser tests. The tests then save PNG files of the whole window there: the sidebar with a task in light and dark mode, the narrow sidebar, two picked tints, black dark mode with a picked tint, an automatic move with its card, the activity card for an unsure answer in an ordinary and in a crowded sidebar, a suggestion with its Confirm button, the theme picker, the Organize card while organizing and paused in light and dark mode, the task card in light and dark mode, a task renamed on its row in the wide and the collapsed sidebar, Name Task at work and failed, and the settings page with its models, a failed connection test with its details, and its theme row.

`LiveLanguageModelBrowserTest.NameTaskWithDeepSeek` sends Name Task to DeepSeek's `deepseek-flash` and checks that a name comes back. It is skipped unless `DEEPSEEK_API_KEY` is set. Put the key in `.env.test` at the repo root, which git ignores, and run from `.chromium/src`:

```sh
(set -a; . ../../.env.test; set +a; ./out/uw/browser_tests --gtest_filter='LiveLanguageModelBrowserTest.*')
```

## Real-window tests

A few browser tests need a real window, because a headless window never takes keyboard focus. Their names start with `MANUAL_`. `pnpm run window-tests` runs them in a macOS VM that has no window on your screen, so they never take your screen or focus:

```sh
pnpm run window-tests
pnpm run window-tests --gtest_filter='CommandBarBrowserTest.MANUAL_*' --gtest_repeat=100
```

Build `browser_tests` first. The command starts the `uw-window-tests` VM, runs the tests in it against your `.chromium/src`, and stops the VM. A second run waits for the first. Options other than `--checkout-dir` go to `browser_tests`; without a `--gtest_filter`, it runs every `MANUAL_` test. With `UW_SCREENSHOT_DIR` set, screenshots land in that folder. If a test crashes, the output names a folder holding its crash report.

### Set up the VM once

The VM uses [Tart](https://tart.run). It takes about 60 GB of disk, and 4 CPUs and 8 GB of memory while it runs.

1. Install Tart. On 2026-10-16 `brew install cirruslabs/cli/tart` failed with current Homebrew, so unpack the [release](https://github.com/cirruslabs/tart/releases) instead:

   ```sh
   curl -LO https://github.com/cirruslabs/tart/releases/download/2.32.1/tart.tar.gz
   tar -xzf tart.tar.gz && mv tart.app ~/Applications/
   ln -s ~/Applications/tart.app/Contents/MacOS/tart ~/.local/bin/tart
   ```

2. Create the VM from the macOS installer pinned in [`chromium/pins.json`](chromium/pins.json). The download is about 27 GB:

   ```sh
   tart create --from-ipsw "$(node -p 'require("./chromium/pins.json").macos_vm_ipsw')" uw-window-tests
   tart set uw-window-tests --cpu 4 --memory 8192
   ```

3. Open it once with `tart run uw-window-tests`. In its window, finish macOS setup with the account `admin` and password `admin`, and decline FileVault. Then turn on **Remote Login** in System Settings > General > Sharing. This is the only time the VM needs a window.

4. Give the command its own SSH key, then turn on automatic login and turn off sleep and the screen lock. `sysadminctl -autologin` fails on macOS 27 (error 22), so the saved-password file is written directly; its bytes encode `admin`. `192.168.64.1` is the Mac's address on the VM network:

   ```sh
   vm() { ssh -b 192.168.64.1 -i ~/.ssh/uw-window-tests admin@"$(tart ip uw-window-tests)" "$@"; }
   ssh-keygen -t ed25519 -N '' -f ~/.ssh/uw-window-tests
   ssh-copy-id -o BindAddress=192.168.64.1 -i ~/.ssh/uw-window-tests admin@"$(tart ip uw-window-tests)"
   vm 'echo admin | sudo -S sh -c "echo 00000000: 1ced 3f4a bcbc ba2c caca 4e82 | xxd -r - /etc/kcpassword && chmod 600 /etc/kcpassword && defaults write /Library/Preferences/com.apple.loginwindow autoLoginUser admin && pmset -a sleep 0 displaysleep 0"'
   vm 'sysadminctl -screenLock off -password admin'
   vm 'echo admin | sudo -S shutdown -h now'
   ```

A VPN that sends every local network to your router, such as Tailscale with an exit node and LAN access on, also sends the VM's network there. The command and the steps above connect from the Mac's own address on that network, so they still reach the VM. The VM then has no internet, which the tests don't need, but macOS setup stalls on its Screen Time page. To give the VM internet during setup, point the route back at the VM; the VPN puts its route back the next time the VM starts:

```sh
sudo route -n delete -net 192.168.64.0/24
sudo route -n add -net 192.168.64.0/24 -interface 192.168.64.1 -cloning
```

## Build inputs and updates

- [`chromium/pins.json`](chromium/pins.json) pins Chromium, `depot_tools`, and the SDK requirement.
- [`chromium/args.gn`](chromium/args.gn) defines a native, unbranded release build that plays H.264 and AAC media, with debug symbols and [development checks](docs/architecture.md#native-lifetime-rules) disabled. Changing it rebuilds every file.
- [`chromium/patches/series`](chromium/patches/series) orders the reviewed uw changes on top of the pin.
- [`browser/`](browser) and [`components/`](components) hold product-owned C++ source linked into the prepared checkout.
- [`pnpm-lock.yaml`](pnpm-lock.yaml) locks the TypeScript tooling dependencies.

The wrapper disables `depot_tools` auto-update. Chromium's pinned `DEPS` supplies its compiler and dependencies.

On Apple Silicon, the wrapper selects Apple's linker with `use_lld = false`. The pinned LLVM linker cannot read SDK 27's `arm64e.x1` targets. Intel keeps Chromium's default linker and remains unverified.

Xcode is a host dependency. Matching source pins alone does not guarantee identical binaries.

To update Chromium, choose a release from [Chromium Dash](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1), verify its upstream tag and commit, and update the pins. Check that revision's SDK requirement, then rerun sync, build, and smoke. Review and refresh any conflicting patches, then run the [customization acceptance checks](#customization-verification).

## CI

- [`checks.yml`](.github/workflows/checks.yml) installs dependencies with pnpm, runs the tooling checks, and validates workflows on a GitHub-hosted runner.
- [`chromium-macos.yml`](.github/workflows/chromium-macos.yml) builds and smoke-tests uw on a self-hosted Mac, then uploads `uw.app` as a ZIP. It runs only when started by hand, because no build Mac is registered yet.

Register a current Actions runner with labels `self-hosted`, `macOS`, `ARM64`, and `uw-chromium`. It needs the build-host requirements above. Set repository variable `UW_CHROMIUM_ROOT` to a dedicated checkout on its build volume. The default is `$HOME/uw-chromium`, outside the Actions repository checkout so sources survive between jobs.

Full builds share a concurrency group. Use a separate checkout for local builds while CI is using its checkout.

## Verification status

### Upstream baseline

Chromium 153.0.8010.53 built and passed the headless smoke test on 2026-09-20.

| Host | Value |
| --- | --- |
| Hardware | Apple M1 Max, 64 GiB RAM |
| macOS | 27.0, build 26A428 |
| Xcode / SDK | Xcode 27.0, build 27A266a / SDK 27.0 |
| Tooling | Node.js 26.8.2, pnpm 10.31.0 |
| Parallel jobs | 8 |

- Source sync took 19 minutes. The successful build command took 5 hours 44 minutes.
- The checkout uses 43 GiB, including 12 GiB of build output. `Chromium.app` is 773 MiB.
- The sandboxed smoke test passed twice. TypeScript checking and all 25 tooling tests pass.

Timings include other applications running and partial build output reused after setup retries.

The smoke test uses incognito because normal-profile `--dump-dom` loaded the page but did not exit on this host. Daily-use stability remains unverified.

### Customization verification

The uw patches, including the manual tab-tree integration, were checked against files retrieved from the exact pinned Chromium commit. The complete series applies, verifies, reverses, and reapplies idempotently. Patched GRIT XML and the macOS plist pass syntax checks. TypeScript checking and all 49 tooling tests pass. The tests cover ordered patches, source links, updates and removals, conflicts, interruption recovery, and preservation of local work.

On 2026-09-24, the customized application built and passed the headless smoke test on a 64 GiB Apple Silicon Mac with Xcode 27.0 and macOS SDK 27.0. The smoke test reported `uw 153.0.8010.53` and passed its isolated-profile navigation and JavaScript check.

On 2026-10-11, the [tab tree](docs/plans/native-tab-tree.md) and the [organizer](docs/plans/automatic-organization.md) were rebuilt on native tab groups. The app built on the same host and passed the headless smoke test. The focused Chromium suites pass: `PageTreeTest.*`, `AiProtocolTest.*`, `TintTest.*`, `VerticalTabStripPrefsTest.*`, and `VerticalTabStripStateControllerTest.*` in `unit_tests`, and `TabOrganizationControllerBrowserTest.*`, `AutomaticOrganizerBrowserTest.*`, `UwOrganizationPageBrowserTest.*`, `WindowScreenshotBrowserTest.*`, and `TintBrowserTest.*` in `browser_tests`. The organizer and settings tests use a local test server.

On 2026-10-12, TypeSafe's live API answered the validation request that uw sends for Jev, sent by hand, in the shape uw expects. Its model list uses `models[].name`, which uw now reads. A DeepSeek chat model validated from the settings page in a real window. With Jev deciding, the same model then failed validation as the namer: it thinks before it answers, and the naming request allowed 32 tokens. Chat requests now allow 4096. That fix has not been rechecked against DeepSeek. The built app's "Test connection" passed against the live Jev API in 0.4 seconds, run headless with a throwaway profile, and showed each provider's own message for a wrong key. Filing tabs with a live provider has not been checked.

The [manual native tab-tree slice](docs/plans/native-tab-tree.md) is approved. Its API investigation and implementation build are complete; native verification remains pending.

Pending native verification:

1. Launch with a fresh profile and no feature flags. Check native vertical tabs, the app menu and About name, tab creation, closing, grouping, dragging, and window resizing.
2. Run the [native tab-tree acceptance checks](docs/plans/native-tab-tree.md#acceptance-checks), including Scratchpad, branch extraction, close promotion, undo, and session restoration.
3. Run the [automatic organization acceptance checks](docs/plans/automatic-organization.md#acceptance-checks) with a configured provider, including classification, generated naming, manual holds, pause, and undo feedback.
4. Check that no menu or setting offers horizontal tabs. Check required extensions separately.

Headless smoke testing does not establish that the native browser interface works.

## Upstream references

- [macOS build instructions](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_build_instructions.md)
- [Apple Silicon builds](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_arm64.md)
- [SDK configuration for the pinned revision](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/build/config/mac/mac_sdk.gni)
- [Apple linker support](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/apple_platform_linkers.md)
