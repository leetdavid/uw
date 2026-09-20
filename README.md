# uw browser

**ur web, ur way**

uw browser is a planned Chromium-based browser. The current implementation is a build foundation for upstream `Chromium.app`; uw's product features remain planned.

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

- `sync` downloads the pinned Chromium source, dependencies, and upstream tools.
- `build` generates the build files and compiles the `chrome` target.
- `smoke` checks the version and runs a headless JavaScript/DOM test in a temporary incognito profile, with the sandbox enabled.

Start with `--jobs 2` on a 16 GiB Mac. `caffeinate` prevents idle sleep during the download and build. Rerun `build` to reuse completed work after an interruption.

Sync and build repeat the host checks. Sync refuses unrelated directories and local edits in Chromium, its tools, or tracked dependencies.

Use `--checkout-dir PATH` instead of `UW_CHROMIUM_ROOT` if preferred. Each command has help:

```sh
pnpm run build --help
```

## Using your local build

After a successful build, launch the application with a separate development profile:

```sh
checkout="$(pnpm --silent run chromium root)"
open -n "$checkout/src/out/uw/Chromium.app" --args \
  --user-data-dir="$HOME/Library/Application Support/uw-dev"
```

Reuse this command to keep your development profile's settings and extensions. For tab restoration, choose "Continue where you left off" at `chrome://settings/onStartup`. Quit this build before rebuilding it.

## Tooling checks

```sh
pnpm run check
```

This runs strict TypeScript checking and the Node.js test suite. These checks do not require Python or a Chromium checkout. Browser compilation and smoke testing are separate checks.

## Build inputs and updates

- [`chromium/pins.json`](chromium/pins.json) pins Chromium, `depot_tools`, and the SDK requirement.
- [`chromium/args.gn`](chromium/args.gn) defines a native, unbranded release build with debug symbols disabled.
- [`pnpm-lock.yaml`](pnpm-lock.yaml) locks the TypeScript tooling dependencies.

The wrapper disables `depot_tools` auto-update. Chromium's pinned `DEPS` supplies its compiler and dependencies.

On Apple Silicon, the wrapper selects Apple's linker with `use_lld = false`. The pinned LLVM linker cannot read SDK 27's `arm64e.x1` targets. Intel keeps Chromium's default linker and remains unverified.

Xcode is a host dependency. Matching source pins alone does not guarantee identical binaries.

To update Chromium, choose a release from [Chromium Dash](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1), verify its upstream tag and commit, and update the pins. Check that revision's SDK requirement, then rerun sync, build, and smoke.

## CI

- [`checks.yml`](.github/workflows/checks.yml) installs dependencies with pnpm, runs the tooling checks, and validates workflows on a GitHub-hosted runner.
- [`chromium-macos.yml`](.github/workflows/chromium-macos.yml) builds and smoke-tests Chromium on a self-hosted Mac, then uploads `Chromium.app` as a ZIP. It runs manually and on build-input changes pushed to `main`.

Register a current Actions runner with labels `self-hosted`, `macOS`, `ARM64`, and `uw-chromium`. It needs the build-host requirements above. Set repository variable `UW_CHROMIUM_ROOT` to a dedicated checkout on its build volume. The default is `$HOME/uw-chromium`, outside the Actions repository checkout so sources survive between jobs.

Full builds share a concurrency group. Use a separate checkout for local builds while CI is using its checkout.

## Verification status

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

## Upstream references

- [macOS build instructions](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_build_instructions.md)
- [Apple Silicon builds](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_arm64.md)
- [SDK configuration for the pinned revision](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/build/config/mac/mac_sdk.gni)
- [Apple linker support](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/apple_platform_linkers.md)
