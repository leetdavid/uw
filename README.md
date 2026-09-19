# uw browser

**ur web, ur way**

uw browser is a planned Chromium-based browser. The current implementation is a build foundation for upstream `Chromium.app`; uw's product features remain planned.

[Product docs and roadmap](docs/README.md)

## Build-host requirements

- macOS on Apple Silicon or Intel.
- Node.js 24.13+; the CI version is pinned in [`.node-version`](.node-version).
- [pnpm](https://pnpm.io/installation), at the version pinned in [`package.json`](package.json).
- Git and working `curl` on PATH.
- Full Xcode, with first-launch setup completed and macOS SDK 26.5 or newer.
- An APFS build volume and a checkout path without spaces.
- 100 GiB free for a fresh checkout, or 50 GiB free for an existing checkout's sync/build.

These are project guardrails, not upstream Chromium requirements. They cover the source checkout, dependencies, generated files, and release-build output with headroom. Start with two build jobs on a 16 GiB Mac; more memory permits greater parallelism.

Chromium's browser core is C++. Our build helper and tests are TypeScript. Chromium's upstream bootstrap requires `python3` on PATH, and `depot_tools` manages its pinned runtime for build hooks and tools. We do not write project-owned Python code.

## Setup

From the repository root:

```sh
pnpm install --frozen-lockfile --ignore-scripts
```

If needed, select full Xcode for the current shell:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
```

The default Chromium checkout is `.chromium/`, outside version control. To use another build volume, set its location before running the commands below:

```sh
export UW_CHROMIUM_ROOT=/Volumes/Build/uw-chromium
```

Use a dedicated, initially empty directory. Keep the same `UW_CHROMIUM_ROOT` setting when building, testing, and running.

Check the host:

```sh
pnpm run doctor
```

`doctor` is read-only. Resolve its failures before fetching Chromium. If `curl` reports that it is a Homebrew-internal shim, use a normal terminal PATH without Homebrew's internal shim directories. macOS provides `/usr/bin/curl`.

## Building

```sh
pnpm run sync
caffeinate -i pnpm run build --jobs 2
pnpm run smoke
```

- `sync` downloads the pinned Chromium source, dependencies, and upstream tools.
- `build` generates the build files and compiles the `chrome` target. `caffeinate` prevents idle sleep during compilation.
- `smoke` checks the built version and runs a headless JavaScript/DOM test in a temporary profile, with the sandbox enabled.

Sync and build both run preflight checks. Sync refuses local source edits and unrelated checkout directories. It also checks dependency worktrees, including previously synced dependencies that have left the current graph.

Commands accept `--checkout-dir` as an alternative to the environment variable. For command-specific help, use, for example:

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

The profile persists at `~/Library/Application Support/uw-dev`, independently of the source checkout. Reuse this launch command to retain its settings and installed extensions. To restore tabs automatically, open `chrome://settings/onStartup` and choose "Continue where you left off". Quit this browser build before rebuilding it.

The application is currently upstream Chromium. The planned uw interface and AI features are described in the [product docs](docs/product.md).

## Tooling checks

```sh
pnpm run check
```

This runs strict TypeScript checking and the Node.js test suite. These checks do not require Python or a Chromium checkout. Browser compilation and smoke testing are separate checks.

## Build inputs and updates

- [`chromium/pins.json`](chromium/pins.json) pins Chromium, `depot_tools`, and the SDK requirement.
- [`chromium/args.gn`](chromium/args.gn) defines a native, unbranded release build with debug symbols disabled.
- [`pnpm-lock.yaml`](pnpm-lock.yaml) locks the TypeScript tooling dependencies.

The wrapper bootstraps the pinned `depot_tools` with auto-update disabled. Chromium's pinned `DEPS` supplies its compiler and other dependencies. System Xcode remains a host prerequisite, so matching source pins alone does not guarantee byte-identical binaries.

To update Chromium, choose a release from [Chromium Dash](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1), verify its upstream tag and commit, and update the pins. Check that revision's SDK requirement, then rerun sync, build, and smoke.

## CI

- [`checks.yml`](.github/workflows/checks.yml) installs dependencies with pnpm, runs the tooling checks, and validates workflows on a GitHub-hosted runner.
- [`chromium-macos.yml`](.github/workflows/chromium-macos.yml) builds and smoke-tests Chromium on a self-hosted Mac, then uploads `Chromium.app` as a ZIP. It runs manually and on build-input changes pushed to `main`.

Register a current Actions runner with labels `self-hosted`, `macOS`, `ARM64`, and `uw-chromium`. It needs the build-host requirements above. Set repository variable `UW_CHROMIUM_ROOT` to a dedicated checkout on its build volume. The default is `$HOME/uw-chromium`, outside the Actions repository checkout so sources survive between jobs.

Full builds share a concurrency group. Use a separate checkout for local builds while CI is using its checkout.

## Verification status

Full Chromium compilation and the browser smoke test remain unverified. Local preflight is blocked by missing full Xcode and approximately 12 GiB free disk space.

Verified tooling includes 25 tests, strict TypeScript checking, and workflow lint. The pinned upstream release and tool revisions were checked; upstream tools were bootstrapped before the helper moved to TypeScript. These results do not establish that Chromium itself compiles yet.

## Upstream references

- [macOS build instructions](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_build_instructions.md)
- [Apple Silicon builds](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/docs/mac_arm64.md)
- [SDK configuration for the pinned revision](https://chromium.googlesource.com/chromium/src/+/792bf6722e73a45aa9e47c163b9901bdc17f3230/build/config/mac/mac_sdk.gni)
