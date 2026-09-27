# uw browser

**ur web, ur way**

uw browser is a Chromium-based browser in development with vertical tabs and AI automation for everyday browsing. [Automatic tab organization](product.md#automatic-tab-organization) is part of the minimum usable version. Planned features include page watches and prompt-built website customizations.

## Docs

- [Product](product.md): experience, headline features, and expected behavior.
- [Glossary](CONTEXT.md): product terminology.
- [Building and using Chromium](../README.md#build-host-requirements): setup, build and run commands, CI, and verification status.
- [Try the current build](try-uw.md): exploratory manual tests for the built app and its current limits.
- [Architecture](architecture.md): technical direction and decisions still to make.
- [Chromium code organization research](research/chromium-organization-research.md): Brave and Helium source layouts, patch workflows, and maintenance tradeoffs.
- [Horse tab organization research](research/horse-tab-organization-research.md): documented tree behavior and open decisions for uw's nested organization.
- [Arc pinned tabs research](research/arc-pinned-tabs-research.md): Favorites, home URLs, reset behavior, and the limits of documented cross-window sharing.
- [Roadmap](plans/roadmap.md): proposed build order, completion criteria, implementability estimates, and a ticket board.
- [Approved native tab-tree slice](plans/native-tab-tree.md): implementation sequence, scope, and native acceptance checks.
- [Approved automatic organization MVP](plans/automatic-organization.md): the automatic tab-filing loop, its agreed scope, and acceptance checks.
- [Chromium tab API research](research/chromium-tab-api-research.md): reusable controls, integration points, and constraints at the pinned revision.
- [ADR: classification requests include page excerpts](adr/0001-classification-requests-include-page-excerpts.md): why classification calls send a short readable-text excerpt.

## Status

The upstream Chromium build and headless smoke test passed on Apple Silicon. The wrapper now prepares minimal uw branding, native vertical tabs by default, and the manual tab-tree slice. The customized app's full build and headless smoke test pass; native UI checks remain pending. The [automatic organization MVP](plans/automatic-organization.md) passes its focused Chromium tests; live-provider and native UI checks remain pending. See [verification status](../README.md#verification-status). Later automation and shared-pin features remain planned.

The product direction, headline features, and decisions marked agreed in [Product](product.md) are confirmed. Remaining behavior and build order are proposals. The current approach uses Chromium source customizations; the update process and local model runtime still need validation or selection.
