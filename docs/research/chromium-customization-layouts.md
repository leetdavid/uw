# Chromium customization layouts

## Scope

Targeted source research on 2026-09-20, not a full architectural audit. Neither external browser was built.
The immediate uw task is a tracked patch that selects upstream vertical tabs by default.

- Brave: [`916bed49404ce4b318ea3053da3a2b974cc5d0ed`][brave-root].
- Helium: [`8c19f4c6d624e31293bca13e655f2fe542ba6fdb`][helium-root].
- Supplementary build entry point: [Helium macOS `10373c4c5e6b0323b56aa676be8a96f9e12342c1`][mac-root]. Its `helium-chromium` submodule pins the Helium commit above.

## Verified mechanisms

### 1. Brave keeps its code inside the Chromium checkout

The sync configuration registers Chromium at `src` and brave-core at `src/brave` as separate unmanaged gclient solutions. Brave targets therefore use `//brave/...` paths. [Sync configuration][brave-sync-config]
Native vertical-tab views are ordinary C++ sources under `browser/ui/views/frame/vertical_tabs/`, with their own GN target and nearby browser tests. [Target definitions][brave-tabs]
WebUI code can live with its component. AI Chat's `components/ai_chat/resources/BUILD.gn` transpiles TSX entry points, preprocesses HTML, and packages resources through GN. [WebUI build][brave-webui]

### 2. Brave's overlays require build integration

`chromium_src/` mirrors upstream paths. A compiler config adds it with `-iquote`, allowing quoted includes to select overrides while angle-bracket includes reach upstream headers. [Compiler config][brave-compiler]
For C++ source files, a patched Siso entry point installs a handler that replaces the compiler's source argument when an override exists. It also records the override as a build input. [Entry patch][brave-siso-patch], [handler][brave-siso]
This is active build machinery, not a directory Chromium understands automatically. Brave documents wrappers around upstream implementations rather than wholesale source copies. [Patching guide][brave-patching]

### 3. Brave patches are primarily per-file diffs, not a feature stack

`update_patches` generates a diff per modified upstream file, flattening its path into the patch filename. It fixes diff settings for reproducible output and can remove stale patches. [Generator][brave-update]
`GitPatcher` reads `.patch` files with `readdir`, without an explicit series or sort, then applies them sequentially with `git apply`. It checks patch/source hashes in `.patchinfo` and checks out affected files before reapplying stale patches. [Applier][brave-apply]
`sync` applies patches after dependency sync and before `gclient runhooks`. The build entry then prepares overrides/branding and calls GN generation and `autoninja`. [Sync][brave-sync], [build][brave-build], [build utilities][brave-util]
For upgrades, the documented workflow changes the Chromium pin, resolves failed patches, regenerates diffs, and reapplies. Some patches are now owned by `rewrite/<upstream-path>.yaml`; the update command normally refuses to overwrite those and directs developers to Plaster. [Upgrade guide][brave-upgrade], [ownership check][brave-update-command]

### 4. Helium separates shared modifications from platform builds

The shared repository owns patches, resources, and development utilities. Platform repositories own packaging and build environments. [Contribution guide][helium-contributing]
On macOS, the shared repository is `helium-chromium`; the generated Chromium tree is `build/src`. `build.sh` calls source preparation, configuration, compilation, and packaging helpers. [Paths][mac-env], [entry point][mac-build]
Preparation applies shared patches before platform patches, then performs substitutions, translations, versioning, and resource copying. Configuration combines shared `flags.gn` with `flags.macos.gn`; compilation invokes upstream `autoninja.py` for `chrome` and `chromedriver`. [Build helpers][mac-shared]

### 5. Helium puts new source inside patches too

`patches/helium/ui/layout/core.patch` both changes upstream files and creates `chrome/browser/ui/helium/helium_layout_state_controller.{h,cc}`. The same patch adds those sources to Chromium's UI GN target. [Native example][helium-layout]
The behavior-settings patch creates TypeScript and HTML files, changes the settings GN target, and wires localized strings and navigation. These additions become ordinary Chromium-tree files after patch application. [WebUI example][helium-webui]
Image assets follow a separate path. `resources/helium_resources.txt` maps source assets to Chromium destinations; `replace_resources.py` copies each pair. The macOS preparation helper invokes this after patching. [Asset map][helium-assets], [copier][helium-copy], [build helpers][mac-shared]

### 6. Helium has explicit patch order and refresh tooling

`patches/series` orders patches across vendor and feature directories. The parser preserves that order; the applier runs GNU `patch -p1 --forward` for each entry. Fuzz is enabled by default, with `--no-fuzz` available. [Series][helium-series], [parser][helium-parser], [applier][helium-patches]
Development tooling prepends shared patches to platform patches, saves the original series, and writes `series.merged`. Unmerge moves edited patches back and restores the separate series with comments. [Merge implementation][helium-merge]
Developers edit `build/src`, use `quilt add` and `quilt refresh`, then unmerge. Chromium upgrades push the stack with `quilt push -a --refresh`, fix failures, and refresh again. [Development and upgrade workflow][mac-development]
The `he pull` implementation pops and unmerges patches, runs `git rebase origin/main` in both repositories, then merges and pushes the stack with refresh. [Development commands][mac-dev]

### 7. Tests follow the code and tooling they exercise

Brave declares local native test targets beside features and aggregates them into executables such as `brave_browser_tests` in `test/BUILD.gn`. Its patcher tests live beside the JavaScript patcher and use temporary Git repositories. [Feature tests][brave-tabs], [test aggregation][brave-tests], [patcher tests][brave-patch-tests]
Helium's shared CI runs utility tests and validates the patch series against Chromium sources. Tests include `utils/tests/test_patches.py` and `devutils/tests/test_validate_patches.py`. These establish tooling checks, not complete browser-behavior coverage. [CI][helium-ci], [utility tests][helium-util-tests], [validator tests][helium-validator-tests]

### 8. A feature gate is not the selected tab layout

Helium's layout patch enables upstream `kVerticalTabs`, but registers its own layout preference with `kClassic` as the default. Its controller uses that preference to select vertical tabs. Copying only the feature-gate change would not reproduce a default-vertical layout. [Gate, preference, and controller][helium-layout]
The inspected versions differ: Brave pins Chromium `154.0.8037.49`, Helium pins `153.0.8010.52`, and uw currently pins `153.0.8010.53`. Check uw's exact upstream code before choosing patch hunks. [Brave pin][brave-package], [Helium pin][helium-version], [uw pin](../../chromium/pins.json)

## Recommendation for uw

Simple Git patches are enough now. Keep the current layout and add only the patch inputs when implementing the feature:

```text
chromium/
  pins.json
  args.gn
  patches/
    series
    vertical-tabs-default.patch
scripts/chromium.ts
tests/chromium.test.ts
.chromium/{src,depot_tools}/   ignored working checkouts
```

- Borrow Helium's explicit series order and feature-sized ownership. One patch may change the feature gate and preference default together. Give it a short purpose and acceptance criteria.
- Keep patch application in the TypeScript wrapper. Establish the pinned checkout, apply the series, then run GN/build. If patches later affect hooks, apply them between `gclient sync --nohooks` and `gclient runhooks`.
- Use `git apply --check` before application. Define repeated application, changed patches, and partial-failure recovery. Include ordered patch contents in completion-state validation so builds reject stale or incomplete customization.
- Preserve the existing refusal of unknown source edits. [`requireCleanWorktrees`](../../scripts/chromium.ts) currently rejects the managed patch diff too. Recognize the expected applied diff explicitly; do not copy Brave's automatic checkout of modified files or merely ignore dirty patched paths.
- Refresh the tracked diff against the pinned base after editing the working checkout. Verify clean-checkout replay, repeat application, rejection of unrelated edits, and patch failure. Verify default vertical tabs in a fresh profile and persistence of a user's later layout choice.

When uw first adds substantial native code, introduce tracked `src/` and copy its owned files into `<checkout>/src/uw/`. Put browser integration under `src/browser/<feature>/`, with GN targets, native tests, and any WebUI `resources/` nearby. Add `src/components/` only for genuinely shared code. Use small upstream call-site and GN dependency patches to reach `//uw/...` targets. This is a proposal, not existing support.

Borrow Brave's separation of new source from upstream edits at that point. Defer source staging until needed. Defer a `chromium_src` override engine, Plaster, quilt tooling, vendor patch groups, and separate platform repositories. Avoid storing large new feature implementations solely as added lines in patches.

Implementability estimates use the [roadmap convention](../plans/roadmap.md#implementability-estimates): **9/10** for the small patch workflow; **8/10** for the narrow upstream vertical-tabs default change. The main checks are managed-edit detection and the feature/preference behavior at uw's pin. These are estimates, not verification results.

Keep uw-owned tooling in TypeScript. Chromium's upstream build tools still require Python. Patch applicability and successful compilation do not prove correct UI behavior or low future rebase cost.

## Primary sources

[brave-root]: https://github.com/brave/brave-core/tree/916bed49404ce4b318ea3053da3a2b974cc5d0ed
[helium-root]: https://github.com/imputnet/helium/tree/8c19f4c6d624e31293bca13e655f2fe542ba6fdb
[mac-root]: https://github.com/imputnet/helium-macos/tree/10373c4c5e6b0323b56aa676be8a96f9e12342c1
[brave-sync-config]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/syncUtils.js#L36-L90
[brave-tabs]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/browser/ui/views/frame/vertical_tabs/BUILD.gn
[brave-webui]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/components/ai_chat/resources/BUILD.gn
[brave-compiler]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/BUILD.gn#L9-L49
[brave-siso-patch]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/patches/build-config-siso-main.star.patch
[brave-siso]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/config/siso/brave_siso_config.star
[brave-patching]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/patching_and_chromium_src.md
[brave-update]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/updatePatches.js
[brave-apply]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/gitPatcher.js
[brave-sync]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/scripts/sync.ts
[brave-build]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/build.ts
[brave-util]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/util.js
[brave-upgrade]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/chromium_version_upgrade.md
[brave-update-command]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/scripts/updatePatches.js#L125-L181
[helium-contributing]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/CONTRIBUTING.md
[mac-env]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/env.sh#L5-L18
[mac-build]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/build.sh
[mac-shared]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/devutils/shared.sh
[helium-layout]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/helium/ui/layout/core.patch
[helium-webui]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/helium/settings/setup-behavior-settings-page.patch
[helium-assets]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/resources/helium_resources.txt
[helium-copy]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/replace_resources.py
[helium-series]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/series
[helium-parser]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/_common.py#L123-L137
[helium-patches]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/patches.py
[helium-merge]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/devutils/update_platform_patches.py
[mac-development]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/docs/building.md#L103-L175
[mac-dev]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/dev.sh
[brave-tests]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/test/BUILD.gn#L1440-L1455
[brave-patch-tests]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/gitPatcher.test.js
[helium-ci]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/.github/workflows/ci.yml
[helium-util-tests]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/tests/test_patches.py
[helium-validator-tests]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/devutils/tests/test_validate_patches.py
[brave-package]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/package.json
[helium-version]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/chromium_version.txt
