# Native tab tree: approved first slice

Status: the user approved this staged plan and implementation of the first slice after the product interview. The source/API investigation is complete; implementation and native verification are pending a usable Chromium build host.

Implementability: **6/10** for the first slice. The [roadmap](roadmap.md#implementability-estimates) scores later automation and shared live pins separately. Scores include integration and maintenance, not progress.

## Scope

Implement the manual browser workflow defined in [Product](../product.md#automatic-tab-organization):

- A native vertical tree with task containers and page-parent branches.
- [Scratchpad](../product.md#scratchpad), including nested ungrouped branches.
- Manual task creation, extraction, renaming, reordering, and nesting.
- Ordinary close with child promotion, plus explicit branch closing.
- [Organization undo](../product.md#organization-undo).
- A native [sidebar button menu](../product.md#organization-controls) exposing the implemented manual commands.

The approval defers Jev/LLM automation and shared live pins to measured prototypes. Their requirements remain in Product. AI-only menu actions become available with the automation stage; do not add no-op handlers for them. The existing Chromium pin area supplies pins in this slice. Arc-style home URLs, shared page ownership, Peek, and the new restart restore offer belong to the later pin work.

## Existing Chromium support

Use the exact [source investigation](../chromium-tab-api-research.md) when implementing and refreshing the patches.

- Keep `TabStripModel` responsible for real tabs and `WebContents` lifetime.
- Reuse `TabView`, the existing selection controller, and native tab-drag infrastructure. They already supply favicons, page-state indicators, close buttons, multi-selection, and ordinary tab actions.
- Use Chromium's tree-model and undo facilities for uw-owned hierarchy metadata and inverse organization actions.
- Chromium's collection model cannot directly represent nested tasks or page parents. Keep that hierarchy distinct from its collection ownership.
- Stock `views::TreeView` supplies editing and expansion but lacks multi-selection and tree-specific dragging. Replacing tab rows with it would lose existing browser behavior.

## Implementation sequence

### 1. Establish a reproducible native test loop

Prepare the pinned customized browser and build it on a host satisfying the [build requirements](../../README.md#build-host-requirements). Record a working baseline before adapting the tab views.

The current host fails preflight and has no managed Chromium checkout. No self-hosted runner is currently registered for this repository. See the latest [verification status](../../README.md#customization-verification).

### 2. Add window-owned hierarchy and commands

Add the first real product module under `browser/tab_organization/`, following the [source layout](../architecture.md#code-organization). Own it from `BrowserWindowFeatures`, with an interface reachable through the window's existing unowned-user-data mechanism.

Track tabs by `tabs::TabHandle`, checking both liveness and current window membership at command execution. Keep stable metadata IDs for tasks and restoration. Capture actual link-opening relationships rather than relying on Chromium's temporary opener bookkeeping.

Implement extraction, nesting/reordering, rename, collapse state, child promotion, and inverse organization operations. Normalize selected parents and descendants into disjoint branches. Keep tab close handling in Chromium so unload cancellation and ordinary history continue to work.

Undo applies to surviving tabs in the same window. Preserve tabs opened since the action and the current page/selection. Closing a tab is handled by Chromium's close/restore system; organization undo must not recreate a destroyed renderer and claim its live state survived.

### 3. Connect native layout and input

Add the tree projection and headers to the existing vertical unpinned region. Retain upstream pinned controls and browser tab rows. The primary layout seam is `UnpinnedTabContainerViewLayout::CalculateVerticalLayout`.

Adapt drag destinations, link-drop positions, focus traversal, and accessibility to match the visible hierarchy. Route the sidebar menu and contextual commands to the same window-owned controller. Use existing native menu, label, button, and text-field controls.

Organization must preserve page identity and browser selection. Move tabs in place when linear order must change; do not detach and recreate pages to organize one window. Metadata must survive view reconstruction and horizontal/vertical orientation changes.

### 4. Integrate source preparation and persistence

Add ordinary tracked product source and explicit GN targets. Extend preparation only enough to expose those real files beneath Chromium's `src/uw`; preserve the existing protection of local work. Keep upstream integration edits in the ordered patch series.

Use Chromium's session extra-data hooks for hierarchy metadata and remap restored tab identities. Verify compatibility with the existing session-restore flow before adding the later pinned-session restore offer.

## Acceptance checks

Use native unit/browser tests for the model and integration. Run UI checks on the built application. Headless DOM smoke alone cannot pass this slice.

1. New ungrouped tabs appear in Scratchpad. New-tab links from regular pages retain parentage; ordinary navigation preserves children.
2. Extraction preserves branch order and hierarchy, creates the sibling task in the specified position, and preserves a page's JavaScript sentinel, form value, and focus.
3. Renaming and manual movement work for tasks and pages. Selection supports multiple tabs. Dragging cannot create cycles or leave the rendered order inconsistent with keyboard selection.
4. Closing a parent promotes surviving children. Branch closing uses normal unload handling, including cancellation, and closing the final tab does not leave dangling callbacks.
5. One undo reverses related organization edits. Intervening tab creation, closing, or movement to another window does not lose pages or move the current selection unexpectedly.
6. Collapse choices, task names, and sibling order survive view reconstruction. Existing pins, native groups/splits, ordinary shortcuts, and horizontal mode remain usable.
7. A restored session recovers hierarchy and expansion metadata using the correct restored tabs.

Run repository checks with `pnpm run check`. On the build host, run `gn check`, compile the browser and relevant test targets, then execute the focused suites identified in the [API research](../chromium-tab-api-research.md#gn-ownership-and-verification).

## Later stages

These remain planned and are outside the current implementation approval:

- **Jev/LLM prototype, 5/10:** validate the actual provider contract, opt-in input scope, accuracy, stale-result handling, and the complete initial latency budget on representative workloads.
- **Shared-pin prototype, 3/10:** prove profile-owned page continuity, window handoff, inactive views, windowless retention, reset/unpin behavior, and recovery before integrating the full pin experience.

Update the roadmap with measured results from those trials before expanding implementation.
