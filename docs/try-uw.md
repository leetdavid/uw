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

Cmd-T opens the [command bar](#command-bar), which is on by default but cannot open pages yet. Several tests below start with Cmd-T. Before you run them, turn off `Cmd+T opens the command bar` at `uw://settings/uwOrganization`, and Cmd-T opens a blank tab again.

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

### Collapse a branch

Open a few links from one page with Cmd-click, so they sit under it.

- Point at the page's tab. Its favicon turns into a chevron. Click the chevron: the tabs under it hide and the row ends in their number.
- Ctrl+Cmd+. does the same for the selected tab, and `Collapse branch` in a tab's right-click menu for that tab. Each turns into `Expand` once the branch is collapsed.
- Ctrl+Tab skips the hidden tabs. Collapsing the branch of the tab you are on selects the page at its top.
- Cmd-click another link on the page: the new tab stays hidden and the number goes up, with no spinner while it loads. A link opened in front expands the branch.
- Quit and reopen uw with "Continue where you left off": the branch is still collapsed.

Expected result: the hidden tabs stay open and come back in place. Closing the collapsed tab shows them one level up.

### New child tab

Right-click a tab that has children but is not selected, and choose `New child tab`, under `New tab below`.

Expected result: a blank tab opens, selected, one level under that tab and above its other children. Ctrl+Cmd+T and `New Child Tab` in the Tab menu do the same for the selected tab. `New tab below` opens below the right-clicked tab and its children, at its level.

### Organize button

Click `Organize` at the foot of the sidebar.

- `New task` opens a new tab in a new task of its own. The `+` button under the tab list and Ctrl+Cmd+P do the same. Cmd-T still opens a tab beside the current one, inside its task.
- `Undo tab organization` reverses the last move, as in the tab menu. Option+Cmd+Z does the same at any time, also after the card above the button has gone.
- The pause items stop and resume automatic organization.
- `Show organizer activity` makes the card above the button show what the model answered for each tab: its choice, how long it took, and the number that decided it against what it had to reach. A suggestion that fell short has a `Confirm` button that carries it out. Each confirmation is written to the organizer log as an `accepted` line: an example of what the model saw and the choice you confirmed.
- `Tab organization settings` opens the settings page.

### AI providers

Open `uw://providers`. `Add provider` offers OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, Jev, and Custom. uw lists the provider's models before it adds it, and the key goes to the Keychain. Then pick a language model, a decision model, or both, and an agent model for chats. Click a provider to see its address, key, format, and models, or to remove it.

Expected result: `Test` beside the language or decision model sends it one small request and says `✓ <model> answered in <seconds> s`, or why it failed; `Details` shows what uw sent and what came back. A newly picked model says `Not tested yet`. The first time this build starts, the models saved in the old settings become providers, and the page says so once.

Until the Anthropic Messages and OpenAI Responses formats land, uw reaches every language model over chat completions, including OpenAI's and Anthropic's. Tool calls and effort levels per model are not shown yet.

### Automatic organization

Pick and test a decision model, a language model, or both at `uw://providers`. Then turn on the switch at the top of `uw://settings/uwOrganization`; it says what it still waits for, such as a model that has not passed its test. Then open a link in a new tab.

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

### Terminal tabs

Choose `New terminal` from the File menu, press Ctrl+\`, or right-click the sidebar's `+` and choose `New terminal`.

Expected result: a tab opens with your login shell in it, named after the shell's folder. Cmd shortcuts, Ctrl+Tab, and Ctrl+\` go to uw, not the shell, and Option works as Alt. A pasted or dropped file types its quoted path. A pasted image is saved to a temporary file, and its path is typed. The organizer files a terminal tab by its title and folder, never by what the shell printed.

### Command bar

With `Cmd+T opens the command bar` on, Cmd-T, File → New Tab, and `New Tab` in the `+` menu open a panel over the page instead of a tab. Type a few letters.

Expected result: suggestions from your history, bookmarks, and search show in the panel. Escape closes it and opens nothing. Picking a suggestion does not open a page yet, and a key pressed within about 40 ms of Cmd-T can be lost. Turn the switch off and Cmd-T opens a blank tab again, in every window at once.

### Chats

A chat asks before it changes anything on a site: a card in the chat names the step, such as `Click button "Place order"?`, with `Approve`, `Not this one`, `Stop the chat`, and a field to tell the agent what to do instead. uw asks on its own before a form's submit button, before pressing Enter in a form's field, and before typing a password or payment details, and the agent marks other changes. When Chromium's list marks a site as sensitive, the chat asks `Let the agent act on <site>?` before the agent uses it. Nothing on a page can answer for you. The agent works only while Safe Browsing is on.

1. Open `uw://providers` and pick an `Agent model` that calls tools. Without one, chats use the language model.
2. Choose `New chat` from the File menu, right-click the sidebar's `+` and choose `New chat`, or type `uw://chat`.
3. Ask for something, such as "What is the top story on Hacker News?"

A new chat asks "What should the agent do?" and suggests three requests: Tab takes the one in the field, and a click takes one of the others. With a language model on `uw://providers`, the language model writes them once a day from your recent chats and most-visited sites, and the chat after the one that asked shows them. Before that, and in a private window, the chat suggests the product's examples.

Expected result: the chat tab takes your first message as its title, and the chat's heading shows it with what the chat is doing: `Working`, then minutes, or `Waits for your approval`. The pages the agent opens appear as child tabs under the chat tab and load in the background, and your selected tab stays selected. Each step shows as a quiet line with what it acted on in bold, such as `Clicked button "Send" in tab 1`, then the answer. When the model request fails, `Try again` asks once more without a new message, and `AI providers` opens `uw://providers`. The agent reads pages as text, and it clicks and types only on https pages. With automatic organization on, the organizer may still move the agent's tabs out from under the chat.

Chats are saved after every step. Quit uw in the middle of one and start it again with "Continue where you left off" set at `chrome://settings/onStartup`. Expected result: the chat tab comes back with everything it showed and the line `Stopped before it finished` with `Continue`. Continue goes on in the same agent tabs.

The chat page lists every saved chat on its left, newest first, with a spinner while one runs, a pause sign when it stopped early, and a filled dot for an answer you have not seen. Click a chat to go to its tab, or to reopen it in a new tab once its tab is closed. `New chat` opens another, the search field filters by title, and the bin at the end of a row deletes the chat and closes its tab.

On the right, a card for each of the agent's tabs shows a picture of the tab after the agent's last step there and what it did. Click a card to go to its tab. `Side by side` puts the chat and the agent's tab next to each other in split view, and the split follows the agent to its other tabs. The sidebar shows the pair as one row in the chat's place, with the agent's other tabs under it. Click it again to end the split.

`Skills` at the foot of the chat list shows every skill the agent can use, from uw's `skills` folder in `~/Library/Application Support/uw-dev` and from `~/.agents/skills`, each with a switch. `Open skills folder` shows uw's folder in Finder. The agent reads a skill's instructions when a request matches its description, and the composer then says which skill it uses. Ask a chat to "make this a skill": a `Save a skill?` card appears, and nothing is written until you click `Save skill`.

## Not available yet

- The last details of the Arc-style look: the shadow around the page card and the slimmer toolbar.
- The brief tint on a tab that was just moved.
- Shared pinned-page sessions, saved home URLs, and Peek previews.
- The Chats section in the sidebar, scheduled chats, and prompt-built site customizations.
- A warning before a terminal that is running a command closes, and terminals that come back in their folder after a restart.
- Verified extension compatibility or daily-use stability.

## Report a problem

Include the version reported by `pnpm run smoke`, your macOS version, whether the profile was new or existing, the exact steps, the expected result, and the actual result. A screenshot or a short screen recording helps for layout, dragging, focus, and restoration problems.

For a crash, also record the quit time, then attach the newest crash report:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/uw*.ips | head -3
```

See the [native tab tree acceptance checks](plans/native-tab-tree.md#acceptance-checks) for the complete engineering verification list.
