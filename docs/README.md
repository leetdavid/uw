# uw browser

**ur web, ur way**

uw browser is a planned Chromium-based browser with vertical tabs and AI automation for everyday browsing. Planned features include automatic tab organization, page watches, and prompt-built website customizations.

## Docs

- [Product](product.md): experience, headline features, and expected behavior.
- [Glossary](CONTEXT.md): product terminology.
- [Building and using Chromium](../README.md#build-host-requirements): setup, build and run commands, CI, and verification status.
- [Architecture](architecture.md): technical direction and decisions still to make.
- [Chromium code organization research](chromium-organization-research.md): Brave and Helium source layouts, patch workflows, and maintenance tradeoffs.
- [Horse tab organization research](horse-tab-organization-research.md): documented tree behavior and open decisions for uw's nested organization.
- [Arc pinned tabs research](arc-pinned-tabs-research.md): Favorites, home URLs, reset behavior, and the limits of documented cross-window sharing.
- [Roadmap](plans/roadmap.md): proposed build order, completion criteria, and implementability estimates.
- [Approved native tab-tree slice](plans/native-tab-tree.md): implementation sequence, scope, and native acceptance checks.
- [Chromium tab API research](chromium-tab-api-research.md): reusable controls, integration points, and constraints at the pinned revision.

## Status

The upstream Chromium build and headless smoke test passed on Apple Silicon. The wrapper now prepares minimal uw branding and native vertical tabs by default. These patches have passed source-level checks; the customized app's build and UI checks are pending. See [verification status](../README.md#verification-status). Later product features remain planned.

The product direction, headline features, and decisions marked agreed in [Product](product.md) are confirmed. Remaining behavior and build order are proposals. The current approach uses Chromium source customizations; the update process and local model runtime still need validation or selection.
