# Building Chromium

The build foundation targets upstream `Chromium.app` on macOS. uw's browser features are still planned.

## Languages

Chromium's browser core is C++. Project-owned build tooling and tests are TypeScript, executed by Node.js. Do not add project-owned Python code.

Chromium's upstream tools, including `gclient` and build hooks, require Python. The build host needs a bootstrap interpreter, and `depot_tools` downloads its pinned runtime. A Chromium source build is not Python-free even though our tooling is TypeScript.

## Prerequisites

- macOS on Apple Silicon or Intel, with Node.js 24.13+, Git, and working `curl` on PATH.
- The upstream Python dependency described above, for Chromium builds only.
- Full Xcode, with first-launch setup completed, supplying macOS SDK 26.5 or newer.
- An APFS build volume and a checkout path without spaces.
- 200 GiB free before a fresh checkout. Existing checkouts require 50 GiB free for sync and build.

The disk thresholds are conservative project budgets, not measured build sizes. Start with two build jobs on a 16 GiB Mac. More memory permits greater parallelism.

Select Xcode for the current shell if needed:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
```

## Local commands

Run from the repository root:

```sh
node scripts/chromium.ts doctor
node scripts/chromium.ts sync
caffeinate -i node scripts/chromium.ts build --jobs 2
node scripts/chromium.ts smoke
```

`doctor` is read-only. `sync` and `build` run it before starting downloads or compilation. `sync` downloads the pinned sources, dependencies, and upstream build tools. `build` generates the build files and compiles the `chrome` target.

The default checkout is `.chromium/`, outside version control. To use another volume, set the same location for all commands:

```sh
export UW_CHROMIUM_ROOT=/Volumes/Build/uw-chromium
node scripts/chromium.ts doctor
```

Each command also accepts `--checkout-dir`. `root` prints the resolved path without creating it. Use a dedicated, initially empty directory. The tooling marks its checkout root and refuses to adopt unrelated files. Sync refuses dirty source checkouts rather than discarding changes. Upstream `gclient` generates its own configuration; the wrapper records its fingerprint and rejects later edits.

The wrapper also keeps a JSON history of dependency roots, so checkouts removed from the current dependency graph remain protected from unnoticed local edits.

The resulting application is `src/out/uw/Chromium.app` inside the checkout. `smoke` checks its version and runs a headless JavaScript/DOM check in a temporary profile. It leaves the browser sandbox enabled.

## Build inputs

- [`chromium/pins.json`](../chromium/pins.json) pins Chromium and `depot_tools` commits and the SDK requirement.
- [`chromium/args.gn`](../chromium/args.gn) defines a native, unbranded release build with debug symbols disabled.

The wrapper bootstraps the pinned `depot_tools` without enabling its auto-update. Chromium's pinned `DEPS` supplies the compiler and other dependencies. The system Xcode installation is a host prerequisite, so matching source pins alone does not promise byte-identical binaries.

To update Chromium, choose a release from [Chromium Dash](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1), verify its upstream tag and commit, and update the pins. Check the SDK requirement for that revision. Rerun sync, build, and smoke before treating the new pin as verified.

## CI

- `checks.yml` runs TypeScript tests, typechecking, and workflow validation on a GitHub-hosted runner. These checks do not need Python.
- `chromium-macos.yml` builds and smoke-tests Chromium on a self-hosted Mac. It runs manually and on build-input changes pushed to `main`, then uploads `Chromium.app` as a ZIP.

Register a current GitHub Actions runner with the labels `self-hosted`, `macOS`, `ARM64`, and `uw-chromium`. It needs the prerequisites above. Set repository variable `UW_CHROMIUM_ROOT` to a dedicated checkout path on its build volume. The default is `$HOME/uw-chromium`, outside the Actions repository checkout so sources survive between jobs.

Full builds share a concurrency group to avoid simultaneous CI access to that checkout. Run local builds in a different checkout while CI is using it.

## Tooling checks

```sh
npm ci --ignore-scripts
npm run check
```

These checks validate the build wrapper. A successful Chromium compile and browser smoke test are separate requirements.

If `curl` reports that it is a Homebrew-internal shim, retry from a normal terminal PATH without Homebrew's internal shim directories. macOS provides `/usr/bin/curl`.

## Verification status

Full compilation and the browser smoke test are pending. The current Mac has 16 GiB RAM, about 12 GiB free on its only mounted build volume, and Command Line Tools with SDK 26.5. Full Xcode is not installed in `/Applications`.

Verified locally:

- Chromium release/tag/commit agreement and the pinned `depot_tools` commit.
- The pinned upstream tools were bootstrapped before the tooling-language change, without changing the pinned commit.
- 25 TypeScript tests, including real Git dependency fixtures and simulated upstream commands.
- Strict TypeScript 7.0.2 typechecking of tooling and tests.
- Both workflows with actionlint 1.7.12.
- The Node.js preflight reports build-host blockers without invoking Python or creating a checkout.

Chromium compilation remains unverified.

There is no Git remote or configured build runner in this checkout yet. The full CI build has not run.

## Upstream references

- [macOS build instructions](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_build_instructions.md)
- [Apple Silicon builds](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_arm64.md)
- [SDK configuration for the pinned revision](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/build/config/mac/mac_sdk.gni)
