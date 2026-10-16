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

Cmd-T opens the [command bar](#command-bar), which is on by default. Several tests below start with a blank tab from Cmd-T. Before you run them, turn off `Cmd+T opens the command bar` at `uw://settings/uwOrganization`, and Cmd-T opens a blank tab again.

## What works now

All of these are manual tests. Record anything that differs from the expected result.

### Tasks are tab groups

1. Open three disposable tabs with Cmd-T.
2. Right-click one and choose `Move to New Task`.

Expected result: the tab moves into a new task named `New task`, above the ungrouped tabs and after any other task. The task shows as a folder row. Point at it: its folder icon turns into a chevron, as a parent tab's favicon does. The ungrouped tabs sit under a `Scratchpad` label at the foot of the list. Rename, recolor, collapse, and drag the task as you would a tab group in Chromium.

Open the task's menu with the button on its row. Expected result: it says task, never group: `New Tab in Task`, `Move Task to New Window`, `Move Tabs to Scratchpad`, and `Close Task`, with no `Delete` and no note that groups save. In a tab's right-click menu, `Move to Task` lists the tasks. Close a task, quit, and start uw again: ⋮ has no `Tab groups` menu and the closed task is not offered anywhere. Cmd+Shift+T still brings back a task you just closed.

### Link parentage and ordinary close

1. Open a page with a harmless link and Cmd-click it.
2. Close the source page with its ordinary close control.

Expected result: the new tab opens directly below its source, indented one level. Closing the source leaves the new tab open and removes its indentation.

### Branch commands

Right-click a tab that opened other tabs.

- `Move to New Task` and `Move to Scratchpad` move the tab together with the tabs it opened.
- `Nest Under Previous Tab` and `Move Up One Level` change which tab it sits under.
- `Close Branch`, under `Close`, closes the tab and everything it opened.
- `Undo Tab Organization` reverses the last of these moves. It does not reopen closed pages.

### Keys and the Tasks menu

Open the `Tab` and `Tasks` menus in the menu bar. Each uw command is there with its key, and the tab's right-click menu shows the same keys.

- Ctrl+Cmd+→ and ← nest the selected tab under the tab above, or move it up a level. Ctrl+Cmd+↑ and ↓ move it and its children past the tab above or below.
- Shift+Ctrl+Cmd+N moves it to a new task, Ctrl+Cmd+S to the Scratchpad, and Option+Cmd+W closes it with its children.
- Ctrl+Cmd+1 to 9 go to the tab last used in that task, and Ctrl+Cmd+0 to the Scratchpad. Ctrl+Cmd+R opens the task's name field.
- The keys also work while a terminal tab has focus.

Expected result: each key does what its menu item says. A page that uses the same key for itself keeps it.

### Collapse a branch

Open a few links from one page with Cmd-click, so they sit under it.

- Point at the page's tab. Its favicon turns into a chevron. Click the chevron: the tabs under it hide and the row ends in their number.
- Ctrl+Cmd+. does the same for the selected tab, and `Collapse Branch` in a tab's right-click menu for that tab. Each turns into `Expand Branch` once the branch is collapsed.
- Ctrl+Tab skips the hidden tabs. Collapsing the branch of the tab you are on selects the page at its top.
- Cmd-click another link on the page: the new tab stays hidden and the number goes up, with no spinner while it loads. A link opened in front expands the branch.
- Quit and reopen uw with "Continue where you left off": the branch is still collapsed.

Expected result: the hidden tabs stay open and come back in place. Closing the collapsed tab shows them one level up.

### New child tab

Right-click a tab that has children but is not selected, and choose `New Child Tab`, under `New Tab Below`.

Expected result: a blank tab opens, selected, one level under that tab and above its other children. Ctrl+Cmd+T and `New Child Tab` in the Tab menu do the same for the selected tab. `New Tab Below` opens below the right-clicked tab and its children, at its level.

### Organize button

Click `Organize` at the foot of the sidebar.

- `New Task` opens a new tab in a new task of its own. Ctrl+Cmd+N does the same. Cmd-T still opens a tab beside the current one, inside its task.
- The new button sits at the right end of the foot. Its `+` does what Cmd-T does. Its `▾` lists `New Tab`, `New Task`, `New Chat`, `New Terminal`, and `New Split View`, each with its key. There is no `+` under the tab list. Collapse the sidebar: a lone `+` stays at its foot, and a right-click on it opens the same menu.
- `Undo Tab Organization` reverses the last move, as in the tab menu. Option+Cmd+Z does the same at any time, also after the card above the button has gone.
- The pause items stop and resume automatic organization. While automatic organization is off they are grayed out, and the settings page hides them.
- `Show Organizer Activity` makes the card above the button show what the model answered for each tab: its choice, how long it took, and the number that decided it against what it had to reach. A suggestion that fell short has a `Confirm` button that carries it out. Each confirmation is written to the organizer log as an `accepted` line: an example of what the model saw and the choice you confirmed.
- `Tab Organization Settings…` opens the settings page.

### AI providers

Open `uw://providers`. `Add provider` offers OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, Jev, and Custom. uw lists the provider's models before it adds it, and the key goes to the Keychain. Then pick a language model, a decision model, or both, and an agent model for chats. Click a provider to see its address, key, format, and models, or to remove it.

Expected result: `Test` beside the language or decision model sends it one small request and says `✓ <model> answered in <seconds> s`, or why it failed; `Details` shows what uw sent and what came back. The first time this build starts, the models saved in the old settings become providers, and the page says so once.

uw reaches OpenAI's models over OpenAI Responses and Anthropic's over Anthropic Messages, so a chat, a task name, or a Test with one of them goes over that format. Every other provider gets chat completions.

A provider's page shows whether each model calls tools and which effort levels it takes, from the [models.dev](https://models.dev) catalog or, for Ollama, from Ollama itself. A Custom provider at an address models.dev lists, such as `https://api.deepseek.com`, gets that provider's levels: DeepSeek's `deepseek-v4-pro` shows `low`, `high`, and `max`. The language and agent models get an effort chip when the model has levels; `default` sends no effort. In the agent model's menu, a model that calls no tools is dimmed and says `No tool calls`.

Every provider's address can change: the Add dialog shows a preset's address filled in, and `Change` on a provider's page takes a new one. Expected result: uw saves it once it lists the models there; otherwise it says why, and the old address stays. An address is https, or plain http on this Mac or your own network, such as `http://192.168.1.20:8317/v1`.

To use [CLIProxyAPI](https://github.com/router-for-me/CLIProxyAPI), add a Custom provider at `http://localhost:8317/v1` with the format `Each model's own`, and the key from its `api-keys` if you set any. Expected result: its page lists each model with the format it goes over: Claude models over Anthropic Messages, GPT models over OpenAI Responses, and the rest over chat completions, with their own effort levels. A Custom provider sends its key as `Authorization: Bearer`, and over Anthropic Messages also as `x-api-key`.

### Automatic organization

Pick a decision model, a language model, or both at `uw://providers`; `Test` is optional. Then turn on the switch at the top of `uw://settings/uwOrganization`; it says so while no model is picked. Then open a link in a new tab.

Expected result: when the model is confident, the tab moves into a task. A card such as `Moved to Plan Lisbon trip` shows above `Organize` for a few seconds, with the tab's title and an `Undo` button. Nothing shows while the model is asked, and nothing when it leaves the tab where it is. If a request fails, the card says so once, such as `The request failed: HTTP 401`, and stays quiet about failures until a request works again. With `Show organizer activity` on, the card also shows each request on its way and the model's numbers.

Then press Cmd-T inside a named task and go to a page that has nothing to do with it.

Expected result: the tab leaves the task. `Moved to Scratchpad` shows for a one-off page such as a sign-in page or a dashboard. A page that clearly starts something of its own moves at once into a task called `New task`, after every other task. A spinner turns on that task's row, and with `Show organizer activity` on the card says `Naming the task`. When the name arrives the task takes it and the card says `Created task …` with `Undo`. Tabs nested under the page go with it, except ones that still fit the old task.

Then choose `New task` from the `Organize` menu and open a page in it.

Expected result: with a language model set, the task called `New task` takes a name within a few seconds of a page that starts a piece of work. A sign-in page, a home page, or a feed names nothing. If the model returns nothing usable, the next such page tries again.

A tab is asked about again when its title changes or it goes to another site. Changing a filter or a sort order in a web app, which only changes the address, asks nothing.

A task that uw named can change its name. Open a few related pages from it that its name is too narrow for, such as hotels in other parts of town in a task named after one neighbourhood. Expected result: the tabs stay and the task gets a wider name. A task you renamed yourself keeps your name. uw remembers which names it gave across a restart.

Open a task's menu with the button on its row and click `Suggest a Name`, the row under the name field. It is there when a language model is set, also while automatic organization is paused or off. Expected result: the row reads `Suggesting a name…` and a spinner turns on the task's row. Within a few seconds the name shows in the field and on the task. The menu can be closed while it waits. The name counts as one uw gave, so a task you had named yourself loses its lock. If the model gives nothing usable, the row reads `Could not suggest a name` and its tooltip says why.

Three sliders on the settings page set how sure a model must be before uw acts without asking: `Move a tab`, `Start a task`, and `Rename a task`. Each shows its number, from 0.50 to 0.95. Drag one down and uw does that one thing more often. Pinned tabs are never sorted.

The [placement rules](plans/automatic-organization.md#placement-rules) and the [thresholds](plans/automatic-organization.md#thresholds) were checked against a test server, by replaying logged requests to the live Jev API, and by letting the built app file made-up pages headless with live Jev. Nobody has watched them file tabs in a real window yet.

### Persistence

Arrange a few tabs, quit uw, and launch it again with "Continue where you left off" set at `chrome://settings/onStartup`. Check that groups and tab order return. This is a trial, not a guarantee.

### Themes

Open the theme picker from View → Theme…, from `Theme…` in the menu of empty space in the sidebar, or from the `Theme` row at `uw://settings/uwOrganization`.

Expected result: a panel opens beside the sidebar. Click a suggested theme, or drag a dot in the color field, and the window changes as you drag. `+` adds a dot, up to three, and dragging a dot out of the field removes it. Text on the window stays white on a dark color and near-black on a light one. `Intensity` grays the colors and `Grain` adds a fine texture around the page card. `Light`, `Dark`, and `Auto` change uw and the color scheme web pages see. In dark mode the window is black unless `Theme colors in dark mode` is on. A color picked in the "Customize Chromium" panel becomes a one-color theme.

### Terminal tabs

Choose `New Terminal` from the File menu or the new button's `▾` menu, or press Ctrl+\`.

Expected result: a tab opens with your login shell in it, named after the shell's folder. Cmd shortcuts, Ctrl+Tab, and Ctrl+\` go to uw, not the shell, and Option works as Alt. A pasted or dropped file types its quoted path. A pasted image is saved to a temporary file, and its path is typed. The organizer files a terminal tab by its title and folder, never by what the shell printed.

### Command bar

With `Cmd+T opens the command bar` on, Cmd-T, File → New Tab, and the new button's `+` open a panel over the page instead of a tab. Type a few letters of a page you visited or have open, then press Return, or use the arrow keys or the mouse to pick another row.

Expected result: the panel lists your open tabs, history, bookmarks, and a search for the typed text, each row labeled Tab, History, Bookmark, or Search. The panel is empty until you type, and nothing typed is sent to the search engine until you pick a row. Return or a click opens the row in a new tab, where Cmd-T's blank tab would have gone; a Tab row switches to that tab. Text with no row picked opens as an address, or as a search. Escape closes the panel and opens nothing, and no key typed right after Cmd-T is lost. Type part of an address you typed twice before, such as `githu`: the rest appears selected after the caret, Tab makes it typed text, and Return opens it. Type `gh` and press Tab to search GitHub, as in Chromium's address bar. Cmd-Return asks uw: a chat opens with your text as its first message, and the agent has the tab you were on as its tab 1. Option-Return runs the text as a shell command in a new terminal tab. Typing `>` first, or opening the panel with Cmd-Shift-P, marks a shell command, and Return runs it the same way. Cmd-K does nothing yet; it names the ticket that brings it. Turn the switch off and Cmd-T opens a blank tab again, in every window at once.

### Chats

A chat asks before it changes anything on a site: a card in the chat names the step, such as `Click button "Place order"?`, with `Approve`, `Not this one`, `Stop the chat`, and a field to tell the agent what to do instead. uw asks on its own before a form's submit button, before pressing Enter in a form's field, and before typing a password or payment details, and the agent marks other changes. When Chromium's list marks a site as sensitive, the chat asks `Let the agent act on <site>?` before the agent uses it. Nothing on a page can answer for you. The agent works only while Safe Browsing is on.

The shield chip in the composer picks how much a chat asks. `Auto`, the default, asks as above. `Ask first` also asks before every click and typing, with the reason `Ask first is on.` `Full auto` never asks, not even for a sensitive site, and the chip turns amber. Expected result: a mode picked on a new chat before its first message is the mode the next new chat starts on; a mode picked later stays with that chat, also after it opens again.

1. Open `uw://providers` and pick an `Agent model` that calls tools. Without one, chats use the language model.
2. Choose `New Chat` from the File menu or the new button's `▾` menu, or type `uw://chat`.
3. Ask for something, such as "What is the top story on Hacker News?"

A new chat asks "What should the agent do?" and suggests three requests: Tab takes the one in the field, and a click takes one of the others. With a language model on `uw://providers`, the language model writes them once a day from your recent chats and most-visited sites, and the chat after the one that asked shows them. Before that, and in a private window, the chat suggests the product's examples.

Expected result: the chat tab takes your first message as its title, and the chat's heading shows it with what the chat is doing: `Working`, then minutes, or `Waits for your approval`. The pages the agent opens appear as child tabs under the chat tab and load in the background, and your selected tab stays selected. Each step shows as a quiet line with what it acted on in bold, such as `Clicked button "Send" in tab 1`, then the answer. The agent may fill in several fields and submit in one go; if a step fails or you decline it, the rest of that go does not run. When the model request fails, `Try again` asks once more without a new message, and `AI providers` opens `uw://providers`. The agent reads pages as text, and it clicks and types only on https pages. With automatic organization on, the organizer may still move the agent's tabs out from under the chat.

Each chat runs on its own model. Its composer has the model chip and, for a model with levels, the effort chip; a new chat starts on the agent model. The model menu groups models by provider, dims the ones that call no tools, and ends with `Manage providers`. Expected result: a switch applies from the next request, even while the agent works, and a thin line such as `Now gpt-5 · high` marks it. The chat keeps its model when it opens again. A chat whose provider was removed, or whose model the provider no longer lists, says so and sends nothing until you pick another model.

Chats are saved after every step. Quit uw in the middle of one and start it again with "Continue where you left off" set at `chrome://settings/onStartup`. Expected result: the chat tab comes back with everything it showed and the line `Stopped before it finished` with `Continue`. Continue goes on in the same agent tabs.

The chat page lists every saved chat on its left, newest first, with a spinner while one runs, a pause sign when it stopped early, and a filled dot for an answer you have not seen. Click a chat to go to its tab, or to reopen it in a new tab once its tab is closed. `New chat` opens another, the search field filters by title, and the bin at the end of a row deletes the chat and closes its tab.

On the right, a card for each of the agent's tabs shows a picture of the tab after the agent's last step there and what it did. Click a card to go to its tab. `Side by side` puts the chat and the agent's tab next to each other in split view, and the split follows the agent to its other tabs. The sidebar shows the pair as one row in the chat's place, with the agent's other tabs under it. Click it again to end the split.

`Skills` at the foot of the chat list shows every skill the agent can use, from uw's `skills` folder in `~/Library/Application Support/uw-dev` and from `~/.agents/skills`, each with a switch. `Open skills folder` shows uw's folder in Finder. The agent reads a skill's instructions when a request matches its description, and the composer then says which skill it uses. Ask a chat to "make this a skill": a `Save a skill?` card appears, and nothing is written until you click `Save skill`.

## Not available yet

- The last details of the Arc-style look: the shadow around the page card and the slimmer toolbar.
- [Themes](plans/themes.md): the picker, themes of more than one color, grain, and black dark mode.
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
