# Try the current build

The current macOS build is for exploratory testing. It builds and passes a headless smoke test, but the tab interface has not completed manual verification. Use a separate development profile and do not rely on it for important browsing data.

The tab tree is being rebuilt on Chromium's own tab groups. The [board](plans/roadmap.md#board) shows which pieces have landed. This page describes only what the current branch does.

## Start uw

Follow the [build instructions](../README.md#building), then run:

```sh
pnpm run launch
```

`launch` starts a new instance and returns immediately. It uses `~/Library/Application Support/uw-dev`, separate from your normal browser profile. Quit every uw window before rebuilding.

A profile with no saved orientation starts with vertical tabs. uw keeps tabs in tree order only in vertical mode.

## What works now

All of these are manual tests. Record anything that differs from the expected result.

### Tasks are tab groups

1. Open three disposable tabs with Cmd-T.
2. Right-click one and choose `Move to new task`.

Expected result: the tab moves into a new tab group named `New task`, below the ungrouped tabs. Rename, recolor, collapse, and drag the group as you would in Chromium.

### Link parentage and ordinary close

1. Open a page with a harmless link and Cmd-click it.
2. Close the source page with its ordinary close control.

Expected result: the new tab opens directly below its source, indented one level. Closing the source leaves the new tab open and removes its indentation.

### Branch commands

Right-click a tab that opened other tabs.

- `Move to new task` and `Move to Scratchpad` move the tab together with the tabs it opened.
- `Nest under previous` and `Move up one level` change which tab it sits under.
- `Close branch` closes the tab and everything it opened.
- `Undo tab organization` reverses the last of these moves. It does not reopen closed pages.

### Persistence

Arrange a few tabs, quit uw, and launch it again with "Continue where you left off" set at `chrome://settings/onStartup`. Check that groups and tab order return. This is a trial, not a guarantee.

## Not available yet

- The Scratchpad label and the Organize button. Use the tab's right-click menu for now.
- Automatic organization. The settings page at `uw://settings/uwOrganization` saves a connection, but nothing files tabs until the organizer is rebuilt.
- Shared pinned-page sessions, saved home URLs, and Peek previews.
- Page watches, delegated browser tasks, and prompt-built site customizations.
- Verified extension compatibility or daily-use stability.

## Report a problem

Include the version reported by `pnpm run smoke`, your macOS version, whether the profile was new or existing, the exact steps, the expected result, and the actual result. A screenshot or a short screen recording helps for layout, dragging, focus, and restoration problems.

For a crash, also record the quit time, then attach the newest crash report:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/uw*.ips | head -3
```

See the [native tab tree acceptance checks](plans/native-tab-tree.md#acceptance-checks) for the complete engineering verification list.
