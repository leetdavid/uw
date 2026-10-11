# Try the current build

The current macOS build is for exploratory testing. It builds and passes a
headless smoke test, but the native tab interface has not completed manual
verification. Use a separate development profile and do not rely on it for
important browsing data.

## Start uw

Follow the [build instructions](../README.md#building), then run:

```sh
pnpm run launch
```

`launch` starts a new instance and returns immediately. It uses
`~/Library/Application Support/uw-dev`, separate from your normal browser
profile. Quit every uw window before rebuilding.

The tab-tree controls appear only with vertical tabs. A profile with no saved
orientation starts vertically. If the sidebar is horizontal, use the tab
strip's context menu to switch back to vertical tabs, then relaunch to check
that the choice persists.

## Try the tab tree

All of these are manual tests. Record anything that differs from the expected
result.

### Create a task

1. Open three disposable tabs with Cmd-T.
2. Look for them under the `Scratchpad` heading in the vertical sidebar. Each new tab opens directly beneath the previously active tab as its sibling.
3. Select one or more regular tabs.
4. Open `Organize` and choose `Move to new task`.
5. Enter a task name and confirm it.

Expected result: the selected tab or branches move below a named task header.
Other ungrouped tabs remain in Scratchpad.

### Rename, reorder, and nest tasks

1. Create a second task.
2. Select a task header, open `Organize`, and choose `Rename task`.
3. Use `Move up` or `Move down` to change its order.
4. Use `Nest under previous` to make it a child of the preceding branch.
5. Use `Move up one level` to reverse that nesting.
6. Use the disclosure control next to a branch with children to collapse and
   expand it.

Expected result: the sidebar reflects each change without reloading pages or
changing the selected page unexpectedly.

### Check link parentage and ordinary close

1. Open a page with a harmless regular link.
2. Cmd-click the link to open it in a new tab.
3. Confirm that the new tab appears below the source page in the tree.
4. Close the source page with its ordinary close control, not `Close branch`.

Expected result: the child page stays open and moves up one level.

### Drag branches

1. Create two named tasks with a tab in each.
2. Drag a task header near the top or bottom edge of another task to reorder
   it.
3. Drag it over the middle of another task to nest it.
4. Drag a branch sideways: pulling right nests it under the row, pulling
   left pulls it back out to the row's parent level or higher.

Expected result: edge drops reorder branches, middle drops nest them, and
horizontal displacement moves branches in and out of nesting levels. Do not
use important tabs for this test.

### Close a branch and undo organization

1. Create a disposable task with several tabs or child branches.
2. Select its header, open `Organize`, and choose `Close branch`.
3. For a separate rearrangement or task creation, choose `Undo tab
   organization` from `Organize`.

Expected result: `Close branch` closes the chosen branch and descendants using
normal browser close handling. Organization undo reverses the latest
organization change; it does not restore pages closed by `Close branch`.

### Check persistence

After creating and arranging a small tree, quit uw and launch it again with
`pnpm run launch`. Check whether task names, order, nesting, and collapse state
return with the restored tabs. This is an important trial, not a completed
compatibility guarantee.

### Crash regression checklist

Run these after any native UI change. Each pins a fixed crash. The app must
not quit unexpectedly during any of them. Each item names its automated
regression test; `pnpm run verify` runs them all against the real browser:

1. Open `Organize`, route to the organization settings, then close the
   settings tab. (`OrganizationSettingsOpensAndCloses`)
2. Open five tabs in quick succession with Cmd-T.
   (`RapidTabInsertionKeepsTreeConsistent`)
3. With two named tasks present, switch to horizontal tabs from the tab
   context menu, then switch back to vertical tabs. The tasks and their tabs
   must survive. (`OrientationTogglePreservesTree`)
4. Create a task, move its tabs back to Scratchpad, and undo. Both tabs must
   remain tracked. (`TaskTeardownAndUndoKeepsTreeConsistent`)
5. With two named tasks present in vertical tabs, close the window. The app
   must not quit unexpectedly. (`WindowCloseWithTasksKeepsTeardownClean`,
   plus `SettingsOpenWindowCloseKeepsTeardownClean` for a window closed with
   the settings page open.)

## Not available yet

Automatic organization is implemented behind setup: open `Organize` and
choose `Set up automatic organization…`, then enter both provider endpoints,
the naming model, and both API keys. Both providers are validated with live
probe requests before organization enables; keys go to macOS Keychain. Until
setup completes, every tab stays in Scratchpad. This flow is exploratory and
has not completed manual verification.

Do not expect these product concepts in the current build:

- User-directed proposals, pause controls, or reorganize-all.
- uw's planned shared pinned-page sessions, saved home URLs, or Peek previews.
- Page watches, delegated browser tasks, and prompt-built site customizations.
- Verified extension compatibility or daily-use stability.

## Report a problem

Include the version reported by `pnpm run smoke`, your macOS version, whether
the profile was new or existing, the exact steps, the expected result, and the
actual result. A screenshot or a short screen recording is useful for layout,
dragging, focus, and restoration problems.

For a crash, also record the quit time, then attach the newest crash report:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/uw*.ips | head -3
```

See the [native tab-tree acceptance checks](plans/native-tab-tree.md#acceptance-checks)
for the complete engineering verification list.
