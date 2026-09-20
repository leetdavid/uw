# Chromium code organization research

Researched 2026-09-20 using GitHub's API through `gh`. Findings describe the pinned upstream code, not features implemented in uw.
Read alongside [architecture](architecture.md), the [docs index](README.md), and the [current build foundation](../README.md).
uw keeps Chromium in an ignored checkout. This note supplies evidence for the [current source-organization guidance](architecture.md#code-organization).

## Inspected snapshots

Default branches were resolved before inspecting files. Every external source link below uses a full commit SHA.

| Repository | Default branch | Inspected commit |
| --- | --- | --- |
| `brave/brave-core` | `master` | `916bed49404ce4b318ea3053da3a2b974cc5d0ed` |
| `imputnet/helium` | `main` | `8c19f4c6d624e31293bca13e655f2fe542ba6fdb` |
| `imputnet/helium-macos` | `main` | `10373c4c5e6b0323b56aa676be8a96f9e12342c1` |
| `imputnet/helium-linux` | `main` | `f3fbbada1758aa620498a9536a8ef0e39f08f900` |
| `imputnet/helium-windows` | `main` | `4d957e25791b07320fdbaf158bf434d4d2c24861` |

These default-branch snapshots target different Chromium versions: Brave `154.0.8037.49`, Helium `153.0.8010.52`. They are not a matched release comparison. [B18], [H10]

## Main comparison

| Question | Brave | Helium |
| --- | --- | --- |
| What does the product repo track? | Ordinary product source, build integration, overrides, and patches. It is checked out at Chromium's `src/brave`. [B1] | Shared patches, resources, configuration, and tooling. Platform repos assemble Chromium separately. [H1] |
| Where does new browser code live? | Product files such as `browser/ai_chat/ai_chat_service_factory.cc` and `components/ai_chat/core/browser/ai_chat_service.cc`. [B2], [B3] | Often inside patches that create files under Chromium paths, including `components/helium_services/` and `chrome/browser/resources/settings/`. [H2] |
| What determines patch order? | Per-file patch generation; the applier enumerates `.patch` files without a `series` manifest or explicit sort. [B7], [B8] | An explicit `patches/series`; shared patches precede platform patches. [H3], [H4], [M2] |
| How are overrides applied? | Include-path precedence and Siso source-argument redirection. [B10], [B11] | Direct source patches plus explicit resource-copy manifests and substitution passes. [H2], [H5], [M2] |
| Where is platform packaging? | Desktop build tooling in `brave-core`, with platform-conditioned external dependencies. [B1], [B17] | Separate macOS, Linux, and Windows repos, each consuming the shared repo. [H1], [M1], [L1], [W1] |

## Brave facts

### Tracked files and responsibility boundaries

- `brave-core` is a nested product checkout, not a tracked copy of all Chromium. Its README requires `project/src/brave`; `package.json` selects the Chromium version. `brave-browser` is described as the issues, releases, and wiki repo. [B1], [B18]
- The AI chat example separates browser-specific construction from feature logic. `browser/ai_chat/ai_chat_service_factory.cc` uses `ProfileKeyedServiceFactory`, profile selection, browser globals, and storage partitions to construct a service defined under `components/ai_chat/core/browser/`. [B2], [B3]
- `components` does not mean UI-free or process-independent. AI chat's `core/browser/BUILD.gn` defines a browser-side static library and unit tests; `resources/BUILD.gn` compiles TSX entry points and packs WebUI resources. [B3], [B4]
- Native vertical-tab Views live under `browser/ui/views/frame/vertical_tabs/`, including a macOS `.mm` file. Shared color infrastructure lives under `ui/color/`. These are concrete boundaries; a single top-level `ui/` does not contain all interface code. [B5], [B6]

### Patches, overlays, and replacements

- `updatePatches.js` generates one patch per modified upstream file, replacing `/` with `-` in its name. For example, `chrome-browser-BUILD.gn.patch` changes `chrome/browser/BUILD.gn`. This groups changes by upstream file rather than product feature. [B7], [B13]
- `gitPatcher.js` reads directory entries, selects `.patch` files, resets affected upstream files, and applies patches serially with `git apply`. `.patchinfo` stores checksums for incremental application and removal handling. Do not infer a dependency-ordered feature stack from its filenames. [B8]
- The dispatcher handles Chromium and separate dependency repositories, including V8, DevTools frontend, and FFmpeg. Current sync runs dependency synchronization, patch application, then hooks. [B9], [B19]
- Some documentation lags the scripts. The patching guide says removed changes need manual patch cleanup; `updatePatches.js` contains stale-patch deletion for full updates. Verify tooling behavior rather than adopting that warning as a current invariant. [B7], [B20]
- Header overrides work because the compiler config adds `-iquote` for `brave/chromium_src`. A quoted upstream include can resolve to Brave's header; an angle-bracket include in that override can reach the original. [B10]
- Source overrides need additional machinery. Siso's `__redirect_cc_handler` finds the argument after `-c` or `/c`, checks for a corresponding `brave/chromium_src` file, replaces the argument, and adds that file to action inputs. Merely creating an overlay directory does not provide this behavior. [B11]
- A concrete wrapper, `chromium_src/chrome/browser/browser_about_handler.cc`, renames an upstream function with a macro, includes the original `.cc`, and adds `brave:` URL rewriting before calling it. The documentation permits complete replacements for some stubs but warns against copying whole upstream implementations that can become stale. [B12], [B20]
- There is now a third mechanism: experimental Plaster. Files under `rewrite/` describe transformations and generate ordinary patches. `rewrite/components/permissions/request_type.h.yaml` adds permission enum entries. Plaster reads the original from Git; substitutions run cumulatively and normally require exactly one match. It currently targets Chromium's `src` repo. [B21], [B22]

### GN integration and tests

- `chrome-browser-BUILD.gn.patch` adds `//brave/browser:core` and imports a Brave dependency list. `brave_defaults.gni` adds `//brave` through `root_extra_deps`. Product sources still need explicit GN target/dependency integration. [B13], [B14]
- Brave's `sources.gni` guidance now discourages injecting substantial source lists into `//chrome/browser` and `//chrome/browser/ui`. It prefers separate targets and interface/implementation separation to address circular dependencies. Adding dependency lists through `.gni` remains acceptable. [B15]
- Feature tests live beside implementation. The AI chat core declares a `unit_tests` source set; vertical tabs declares browser and interactive UI tests. `test/BUILD.gn` wires vertical-tab tests into executable suites. Documented runners include `brave_components_unittests`, `brave_unit_tests`, `brave_browser_tests`, Chromium suites, and `test-unit` for JavaScript/TypeScript. [B3], [B5], [B16], [B23]

## Helium facts

### Shared repo versus assembled Chromium

- The shared repo is a patch distribution, not a normal C++ source tree with top-level `browser/` and `components/`. Its contribution guide says most code changes are quilt patches: edit Chromium, refresh the patch, then unmerge and commit. [H1]
- On macOS, `helium-chromium` is the shared Git submodule; `build/src` holds assembled Chromium and `build/src/out/Default` holds output. `build/` is ignored. Source retrieval supports a Chromium archive or clone, so this build tree is not necessarily a Git worktree. [M1], [M3], [M4], [M5]
- Linux and Windows also declare the shared submodule. Their scripts apply shared patches before platform patches and combine shared GN flags with platform flags. Windows uses a Python build entrypoint; Linux uses shell orchestration. [L1], [L2], [W1], [W2]

### Patch content and application order

- `patches/series` is executable ordering data. It includes vendor groups such as ungoogled-chromium and Brave before Helium patches. Its `helium/core`, `helium/settings`, and `helium/ui` paths classify patches, not final source directories. [H3], [H4]
- `helium/core/services-prefs.patch` spans strings, preference registration, native WebUI handlers, a new `components/helium_services/BUILD.gn`, and new settings HTML/TypeScript files. Thus even a `core` patch contains UI and build integration. [H2]
- `helium/ui/layout/vertical.patch` modifies Chromium Views code and adds `vertical_new_tab_button.cc/.h` plus GN entries under `chrome/browser/ui/views/tabs/vertical/`. Helium's visible browser UI changes are not evidence of a separate HTML browser shell. [H6]
- The patch utility follows series entries in order and applies each with GNU `patch -p1 --forward --ignore-whitespace`. Fuzz is enabled by default; `--no-fuzz` disables it. The macOS release preparation shown here does not pass `--no-fuzz`. [H4], [M2]
- Development merges shared patches before platform patches into `series.merged`. Unmerge restores ownership; new patches before the platform block go back to the shared repo. That placement has meaning beyond visual grouping. [H7]

### Overlays, build integration, and checks

- macOS release preparation orders work as source retrieval, pruning/toolchain preparation, shared and platform patches, domain substitution, name substitution, translations, version generation, then resources. It concatenates shared and macOS GN flags, runs `gn gen --fail-on-unused-args`, and invokes upstream `autoninja.py` for `chrome` and `chromedriver`. [M2]
- Resource replacement is a physical copy operation. `replace_resources.py` reads source/destination pairs and calls `shutil.copyfile`; the manifest maps `branding/product_logo.svg` to `chrome/app/theme/chromium/product_logo.svg`. This differs from Brave's compile-time source selection. [H5], [H8]
- Development setup is not identical to release preparation. `he setup` installs resources before pushing the merged quilt stack; substitutions and translations have separate commands. Reproducing one path does not establish that the other path works. [M6], [M2]
- Shared CI runs tooling tests, configuration validation, patch validation against retrieved Chromium, and generated file-list checks. These jobs do not demonstrate browser behavior. macOS's sanity script rejects patch offsets, then expects a build to survive until a 30-second timeout; that is not a completed build or browser-test run. [H9], [M7]

## Maintenance implications and recommendations to evaluate

The points below are research judgments, conditional on uw choosing a source-fork approach.

- Favor ordinary tracked files for substantial new feature logic, with narrow integration patches. Brave's service/factory example makes ownership and unit testing easier to navigate. Helium's patch-centric approach keeps feature changes together, but new source remains embedded in diffs until preparation. [B2], [B3], [H2]
- Specify preparation order and ownership before choosing directory names. A future uw preparation step should distinguish product files, upstream modifications, resource copies, and generated output, and reproduce them from a clean pinned checkout. Both projects need more than a patches directory. [B9], [B11], [M2]
- Start any experiment with explicit GN targets and one representative browser integration test. Avoid importing Brave's whole override system before proving a need; its include precedence, Siso input tracking, and macro wrappers are additional maintenance obligations. [B10], [B11], [B12], [B15]
- Prefer explicit patch ordering if patches may overlap. Helium's series supplies that contract; Brave's per-file generator solves a different problem. For a uw trial, require clean application without fuzz and review every refreshed patch after a Chromium update. [B7], [B8], [H4]
- Treat full source replacements as ownership of future upstream fixes. A wrapper that includes upstream retains more upstream code, but Brave itself documents hidden or unintended macro replacements as a motivation for Plaster. Neither mechanism proves low update cost without an actual version-bump trial. [B20], [B21]
- Separate platform configuration from shared behavior first. Separate platform repositories add submodule-pin and release coordination; Helium demonstrates this arrangement, while Brave demonstrates platform-conditioned integration in one core repo. Repository splitting is an independent decision. [M1], [L1], [W1], [B17]
- Both projects own Python tooling. Copying their build scripts wholesale would conflict with uw's TypeScript-tooling rule. Chromium's upstream Python dependency still remains, as documented in uw's [README](../README.md#build-host-requirements).

Illustrative trial estimates use the [roadmap's convention](plans/roadmap.md#implementability-estimates), not measured results or new commitments: ordinary product files with a small ordered patch set **7/10**; shared/platform patch-series organization **7/10**; a Brave-style general override/rewrite system **4/10**. Ongoing Chromium updates dominate the uncertainty.
The missing evidence for uw is a completed customized build followed by a Chromium update, measuring patch repair and browser-test behavior. See [current verification status](../README.md#customization-verification).

## Primary sources

[B1]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/README.md
[B2]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/browser/ai_chat/ai_chat_service_factory.cc
[B3]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/components/ai_chat/core/browser/BUILD.gn
[B4]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/components/ai_chat/resources/BUILD.gn
[B5]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/browser/ui/views/frame/vertical_tabs/BUILD.gn
[B6]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/ui/color/BUILD.gn
[B7]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/updatePatches.js
[B8]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/gitPatcher.js
[B9]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/lib/util.js#L35-L150
[B10]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/BUILD.gn
[B11]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/config/siso/brave_siso_config.star#L264-L323
[B12]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/chromium_src/chrome/browser/browser_about_handler.cc
[B13]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/patches/chrome-browser-BUILD.gn.patch
[B14]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/args/brave_defaults.gni
[B15]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/gni_sources.md
[B16]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/running_test_suites.md
[B17]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/DEPS
[B18]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/package.json
[B19]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/build/commands/scripts/sync.ts#L124-L150
[B20]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/patching_and_chromium_src.md
[B21]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/docs/plaster.md
[B22]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/rewrite/components/permissions/request_type.h.yaml
[B23]: https://github.com/brave/brave-core/blob/916bed49404ce4b318ea3053da3a2b974cc5d0ed/test/BUILD.gn
[H1]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/CONTRIBUTING.md
[H2]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/helium/core/services-prefs.patch
[H3]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/series
[H4]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/patches.py
[H5]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/utils/replace_resources.py
[H6]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/patches/helium/ui/layout/vertical.patch
[H7]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/devutils/update_platform_patches.py
[H8]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/resources/helium_resources.txt
[H9]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/.github/workflows/ci.yml
[H10]: https://github.com/imputnet/helium/blob/8c19f4c6d624e31293bca13e655f2fe542ba6fdb/chromium_version.txt
[M1]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/.gitmodules
[M2]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/devutils/shared.sh
[M3]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/env.sh
[M4]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/.gitignore
[M5]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/retrieve_and_unpack_resource.sh
[M6]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/dev.sh
[M7]: https://github.com/imputnet/helium-macos/blob/10373c4c5e6b0323b56aa676be8a96f9e12342c1/.github/scripts/sanity-check.sh
[L1]: https://github.com/imputnet/helium-linux/blob/f3fbbada1758aa620498a9536a8ef0e39f08f900/.gitmodules
[L2]: https://github.com/imputnet/helium-linux/blob/f3fbbada1758aa620498a9536a8ef0e39f08f900/scripts/shared.sh
[W1]: https://github.com/imputnet/helium-windows/blob/4d957e25791b07320fdbaf158bf434d4d2c24861/.gitmodules
[W2]: https://github.com/imputnet/helium-windows/blob/4d957e25791b07320fdbaf158bf434d4d2c24861/build.py
