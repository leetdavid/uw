# Try the current build

The current macOS build is for exploratory testing. It builds and passes a headless smoke test, but the tab interface has not completed manual verification. Use a separate development profile and do not rely on it for important browsing data.

The tab tree is being rebuilt on Chromium's own tab groups. The [board](plans/roadmap.md#board) shows which pieces have landed. This page describes only what the current branch does.

## Start uw

Follow the [build instructions](../README.md#building), then run:

```sh
pnpm run launch
```

`launch` starts a new instance and returns immediately. It uses `~/Library/Application Support/uw-dev`, separate from your normal browser profile. Quit uw before rebuilding and before launching a new build: press Cmd-Q, or run `pnpm run kill`, which asks every running copy of this build to quit and stops any that has not after 5 seconds. When uw is running, `launch` [offers to quit it and start it again](../README.md#using-your-local-build).

`launch` also records what the organizer asks the models and what they answer. Each request and its answer is appended to `~/Library/Application Support/uw-dev/organizer-log.jsonl`. The file holds the titles, URLs, and text excerpts of the tabs the organizer looks at, and it stays on this computer. It grows until you delete it. `pnpm run launch:nolog` starts uw without it.

A profile with no saved orientation starts with vertical tabs. uw keeps tabs in tree order only in vertical mode.

## What works now

All of these are manual tests. Record anything that differs from the expected result.

### Tasks are tab groups

1. Open three disposable tabs with Cmd-T.
2. Right-click one and choose `Move to new task`.

Expected result: the tab moves into a new tab group named `New task`, above the ungrouped tabs and after any other task. The group shows as a folder row. The ungrouped tabs sit under a `Scratchpad` label at the foot of the list. Rename, recolor, collapse, and drag the group as you would in Chromium.

### Link parentage and ordinary close

1. Open a page with a harmless link and Cmd-click it.
2. Close the source page with its ordinary close control.

Expected result: the new tab opens directly below its source, indented one level. Closing the source leaves the new tab open and removes its indentation.

### Branch commands

Right-click a tab that opened other tabs.

- `Move to new task` and `Move to Scratchpad` move the tab together with the tabs it opened.
- `Nest under previous tab` and `Move up one level` change which tab it sits under.
- `Close branch` closes the tab and everything it opened.
- `Undo tab organization` reverses the last of these moves. It does not reopen closed pages.

### Keys and the Tasks menu

Open the `Tab` and `Tasks` menus in the menu bar. Each uw command is there with its key, and the tab's right-click menu shows the same keys.

- Ctrl+Cmd+→ and ← nest the selected tab under the tab above, or move it up a level. Ctrl+Cmd+↑ and ↓ move it and its children past the tab above or below.
- Ctrl+Cmd+N moves it to a new task, Ctrl+Cmd+S to the Scratchpad, and Option+Cmd+W closes it with its children.
- Ctrl+Cmd+1 to 9 go to the tab last used in that task, and Ctrl+Cmd+0 to the Scratchpad. Ctrl+Cmd+R opens the task's name field.
- The keys also work while a terminal tab has focus.

Expected result: each key does what its menu item says. A page that uses the same key for itself keeps it.

### Organize button

Click `Organize` at the foot of the sidebar.

- `New task` opens a new tab in a new task of its own. The `+` button under the tab list and Ctrl+Cmd+P do the same. Cmd-T still opens a tab beside the current one, inside its task.
- `Undo tab organization` reverses the last move, as in the tab menu. Option+Cmd+Z does the same at any time, also after the card above the button has gone.
- The pause items stop and resume automatic organization.
- `Show organizer activity` makes the card above the button show what the model answered for each tab: its choice, how long it took, and the number that decided it against what it had to reach. A suggestion that fell short has a `Confirm` button that carries it out. Each confirmation is written to the organizer log as an `accepted` line: an example of what the model saw and the choice you confirmed.
- `Tab organization settings` opens the settings page.

### Automatic organization

Save and validate a decision model, a language model, or both at `uw://settings/uwOrganization`. `Test connection` checks one model by itself without saving, and `Details` shows what the model answered and what uw sent. The switch at the top of the page turns automatic organization off and on again. Then open a link in a new tab.

Expected result: a line such as `Asking jev-latest about Hotels in Lisbon…` shows above `Organize` while the model is asked. When the model is confident, the tab moves into a task. A message such as `Moved to Plan Lisbon trip` shows above `Organize` for a few seconds, with an `Undo` button.

Then press Cmd-T inside a named task and go to a page that has nothing to do with it.

Expected result: the tab leaves the task. `Moved to Scratchpad` shows for a one-off page such as a sign-in page or a dashboard. A page that clearly starts something of its own moves at once into a task called `New task`, after every other task. A spinner turns on that task's row and the card says `Asking … for a name`. When the name arrives the task takes it and the card says `Created task …` with `Undo`. Tabs nested under the page go with it, except ones that still fit the old task.

Then choose `New task` from the `Organize` menu and open a page in it.

Expected result: with a language model set, the task called `New task` takes a name within a few seconds of a page that starts a piece of work. A sign-in page, a home page, or a feed names nothing. If the model returns nothing usable, the next such page tries again.

A tab is asked about again when its title changes or it goes to another site. Changing a filter or a sort order in a web app, which only changes the address, asks nothing.

A task that uw named can change its name. Open a few related pages from it that its name is too narrow for, such as hotels in other parts of town in a task named after one neighbourhood. Expected result: the tabs stay and the task gets a wider name. A task you renamed yourself keeps your name. uw remembers which names it gave across a restart.

Open a task's menu with the button on its row and click `Suggest a name`, the row under the name field. It is there when a language model is set, also while automatic organization is paused or off. Expected result: the row reads `Suggesting a name…` and a spinner turns on the task's row. Within a few seconds the name shows in the field and on the task. The menu can be closed while it waits. The name counts as one uw gave, so a task you had named yourself loses its lock. If the model gives nothing usable, the row reads `Could not suggest a name` and its tooltip says why.

Three sliders on the settings page set how sure a model must be before uw acts without asking: `Move a tab`, `Start a task`, and `Rename a task`. Each shows its number, from 0.50 to 0.95. Drag one down and uw does that one thing more often. Pinned tabs are never sorted.

The [placement rules](plans/automatic-organization.md#placement-rules) and the [thresholds](plans/automatic-organization.md#thresholds) were checked against a test server, by replaying logged requests to the live Jev API, and by letting the built app file made-up pages headless with live Jev. Nobody has watched them file tabs in a real window yet.

### Persistence

Arrange a few tabs, quit uw, and launch it again with "Continue where you left off" set at `chrome://settings/onStartup`. Check that groups and tab order return. This is a trial, not a guarantee.

### Window tint

Open `uw://settings/uwOrganization` and scroll to `Window tint`. Click a color, or use the last swatch to pick any color.

Expected result: the window around the page card becomes a gradient that starts at your color. Text on it is white on a dark color and near-black on a light one. If you pick a color in between, such as a medium blue, uw makes it a little lighter or darker and the page tells you which color it used. `Use the default` returns to lilac in light mode and indigo in dark mode.

## Not available yet

- The last details of the Arc-style look: the shadow around the page card and the slimmer toolbar.
- The brief tint on a tab that was just moved.
- Shared pinned-page sessions, saved home URLs, and Peek previews.
- Chats, scheduled chats, and prompt-built site customizations.
- Verified extension compatibility or daily-use stability.

## Report a problem

Include the version reported by `pnpm run smoke`, your macOS version, whether the profile was new or existing, the exact steps, the expected result, and the actual result. A screenshot or a short screen recording helps for layout, dragging, focus, and restoration problems.

For a crash, also record the quit time, then attach the newest crash report:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/uw*.ips | head -3
```

See the [native tab tree acceptance checks](plans/native-tab-tree.md#acceptance-checks) for the complete engineering verification list.
