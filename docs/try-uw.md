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

Expected result: the tab moves into a new task named `New task`, above the ungrouped tabs and after any other task. The task shows as a folder row. Point at it: its folder icon turns into a chevron, as a parent tab's favicon does. The ungrouped tabs sit under a `Scratchpad` label at the foot of the list. Collapse and drag the task as you would a tab group in Chromium.

Double-click the task's name. Expected result: the name turns into a field on the row with all of it selected, and the task stays open or collapsed as it was. Type a name and press Return, or click elsewhere, to save it. Esc puts the old name back. With the sidebar hidden, the floating sidebar stays in until you are done.

Open the task's card with the ⋮ button on its row, or right-click the row. Expected result: a card opens under the row with `Rename Task`, `Name Task` when a language model is set, a row of color dots with the task's color ringed, `New Tab in Task`, `Move Task to New Window`, `Move Tabs to Scratchpad`, and `Close Task`. It says task, never group, and has no `Delete` and no note that groups save. The arrow keys move between rows, Return runs one, and Esc closes the card. In a tab's right-click menu, `Move to Task` lists the tasks. Close a task, quit, and start uw again: ⋮ has no `Tab groups` menu and the closed task is not offered anywhere. Cmd+Shift+T still brings back a task you just closed.

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
- Ctrl+Cmd+1 to 9 go to the tab last used in that task, and Ctrl+Cmd+0 to the Scratchpad. Ctrl+Cmd+R turns the name on the task's row into a field.
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

### Drag tabs in and out of branches

Open a few links from one page with Cmd-click, so they sit under it.

- Drag the page's tab: the tabs under it come along, still under it.
- Hold Option while you drag: the page moves alone, and its tabs move up a level.
- Collapse the branch first: it moves with its hidden tabs and stays collapsed.
- Drag the last tab under the page and move the pointer left: it leaves the branch. Move it right instead: it goes under the tab above. Each 16px is a level.
- Drag a tab straight down past other tabs: it never ends up under one of them.
- Turn off `Drag sideways to nest tabs` under Mouse on uw's settings page: moving sideways does nothing.

Expected result: the indent follows the pointer during the drag, and the drop keeps it. A dragged branch keeps its shape. After the drop, only the tab you dragged is selected.

### New child tab

Right-click a tab that has children but is not selected, and choose `New Child Tab`, under `New Tab Below`.

Expected result: a blank tab opens, selected, one level under that tab and above its other children. Ctrl+Cmd+T and `New Child Tab` in the Tab menu do the same for the selected tab. `New Tab Below` opens below the right-clicked tab and its children, at its level.

### Organize button

Click `Organize` at the foot of the sidebar. A card opens above it.

- Its first line says what automatic organization is doing: `Organizing`, `Paused, resuming in 42 min`, `Paused until uw restarts`, `Paused`, `Off`, or `Needs a model`. The countdown follows the clock while the card is open.
- While organizing, `Pause` offers `1 hour`, `Until restart`, and `Until I resume`. While paused, the row is `Resume`. While off, it is `Turn On`, and while no model is set, `Set Up`, which opens `uw://providers`.
- Ctrl+Cmd+N opens a new tab in a new task of its own, with the task's name field ready, and so does `New Task` in the new button's menu. Cmd-T still opens a tab beside the current one, inside its task.
- The new button sits at the right end of the foot. Its `+` does what Cmd-T does. Its `▾` lists `New Tab`, `New Task`, `New Chat`, `New Terminal`, and `New Split View`, each with its key. There is no `+` under the tab list.
- `Undo Move` names the move it reverses, such as `SFO to LIS flights → Plan Lisbon trip`, and reverses it, as in the tab menu. Option+Cmd+Z does the same at any time. With nothing to undo, the row is grayed and says `Nothing to undo`.
- The `Show Activity` switch makes the card above the button show what the model answered for each tab: its choice, how long it took, and the number that decided it against what it had to reach. A suggestion that fell short has a `Confirm` button that carries it out. Each confirmation is written to the organizer log as an `accepted` line: an example of what the model saw and the choice you confirmed.
- `Settings…` opens the settings page.

### AI providers

Open `uw://providers`. `Add provider` offers OpenAI, Anthropic, OpenRouter, Vercel AI Gateway, Ollama, Jev, and Custom. uw lists the provider's models before it adds it, and the key goes to the Keychain. Then pick a language model, a decision model, or both, and an agent model for chats. Click a provider to see its address, key, format, and models, or to remove it.

Expected result: `Test` beside the language or decision model sends it one small request and says `✓ <model> answered in <seconds> s`, or why it failed; `Details` shows what uw sent and what came back. The first time this build starts, the models saved in the old settings become providers, and the page says so once.

uw reaches OpenAI's models over OpenAI Responses and Anthropic's over Anthropic Messages, so a chat, a task name, or a Test with one of them goes over that format. Every other provider gets chat completions.

A provider's page shows whether each model calls tools and which effort levels it takes, from the [models.dev](https://models.dev) catalog or, for Ollama, from Ollama itself. A Custom provider at an address models.dev lists, such as `https://api.deepseek.com`, gets that provider's levels: DeepSeek's `deepseek-v4-pro` shows `low`, `high`, and `max`. The language and agent models get an effort chip when the model has levels; `default` sends no effort. In the agent model's menu, a model that calls no tools is dimmed and says `No tool calls`.

Every provider's address can change: the Add dialog shows a preset's address filled in, and `Change` on a provider's page takes a new one. Expected result: uw saves it once it lists the models there; otherwise it says why, and the old address stays. An address is https, or plain http on this Mac or your own network, such as `http://192.168.1.20:8317/v1`.

To use [CLIProxyAPI](https://github.com/router-for-me/CLIProxyAPI), add a Custom provider at `http://localhost:8317/v1` with the format `Each model's own`, and the key from its `api-keys` if you set any. Expected result: its page lists each model with the format it goes over: Claude models over Anthropic Messages, GPT models over OpenAI Responses, and the rest over chat completions, with their own effort levels. A Custom provider sends its key as `Authorization: Bearer`, and over Anthropic Messages also as `x-api-key`.

### Automatic organization

Pick a decision model, a language model, or both at `uw://providers`; `Test` is optional. Then turn on the switch at the top of `uw://settings/uwOrganization`; it says so while no model is picked. Then open a link in a new tab.

Expected result: when the model is confident, the tab moves into a task. A card such as `Moved to Plan Lisbon trip` shows above `Organize` for a few seconds, with the tab's title and an `Undo` button. Nothing shows while the model is asked, and nothing when it leaves the tab where it is. If a request fails, the card says so once, such as `The request failed: HTTP 401`, and stays quiet about failures until a request works again. With `Show Activity` on, the card also shows each request on its way and the model's numbers.

Then press Cmd-T inside a named task and go to a page that has nothing to do with it.

Expected result: the tab leaves the task. `Moved to Scratchpad` shows for a one-off page such as a sign-in page or a dashboard. A page that clearly starts something of its own moves at once into a task called `New task`, after every other task. A spinner turns on that task's row, and with `Show Activity` on the card says `Naming the task`. When the name arrives the task takes it and the card says `Created task …` with `Undo`. Tabs nested under the page go with it, except ones that still fit the old task.

Then press Ctrl+Cmd+N and open a page in the new task.

Expected result: with a language model set, the task called `New task` takes a name within a few seconds of a page that starts a piece of work. A sign-in page, a home page, or a feed names nothing. If the model returns nothing usable, the next such page tries again.

A tab is asked about again when its title changes or it goes to another site. Changing a filter or a sort order in a web app, which only changes the address, asks nothing.

A task that uw named can change its name. Open a few related pages from it that its name is too narrow for, such as hotels in other parts of town in a task named after one neighbourhood. Expected result: the tabs stay and the task gets a wider name. A task you renamed yourself keeps your name. uw remembers which names it gave across a restart.

Open a task's card and click `Name Task`, or choose Tasks → Name Task in the menu bar. It is there when a language model is set, also while automatic organization is paused or off. Expected result: the card closes and a spinner turns on the task's row. Within a few seconds the name shows on the row, with no window open. The name counts as one uw gave, so a task you had named yourself loses its lock. Start renaming the task while it spins, and your name is kept. If the model gives nothing usable, the card above `Organize` says `Couldn't name …` with the reason and `Try Again`, even with `Show Activity` off.

Three sliders on the settings page set how sure a model must be before uw acts without asking: `Move a tab`, `Start a task`, and `Rename a task`. Each shows its number, from 0.50 to 0.95. Drag one down and uw does that one thing more often. Pinned tabs are never sorted.

The [placement rules](plans/automatic-organization.md#placement-rules) and the [thresholds](plans/automatic-organization.md#thresholds) were checked against a test server, by replaying logged requests to the live Jev API, and by letting the built app file made-up pages headless with live Jev. Nobody has watched them file tabs in a real window yet.

### Persistence

Arrange a few tabs, quit uw, and launch it again with "Continue where you left off" set at `chrome://settings/onStartup`. Check that groups and tab order return. This is a trial, not a guarantee.

### Themes

Open the theme picker from View → Theme…, from `Theme…` in the menu of empty space in the sidebar, or from the `Theme` row at `uw://settings/uwOrganization`.

Expected result: a panel opens beside the sidebar. Click a suggested theme, or drag a dot in the color field, and the window changes as you drag. `+` adds a dot, up to three, and dragging a dot out of the field removes it. Text on the window stays white on a dark color and near-black on a light one. `Intensity` grays the colors and `Grain` adds a fine texture around the page card. `Light`, `Dark`, and `Auto` change uw and the color scheme web pages see. `Pages` sets everything outside the sidebar: with `Light` above and `Pages` on `Dark`, websites with a dark look of their own, extensions, uw's and Chromium's pages such as Settings, and Chromium's menus and bubbles turn dark, while the sidebar, its menus and cards, and the theme picker stay light. A site with no dark look stays light. Each workspace keeps its own. In dark mode the window is black unless `Theme colors in dark mode` is on. A color picked in the "Customize Chromium" panel becomes a one-color theme.

### Mini player

Play a video, then use the site's picture-in-picture button, or right-click the video twice and choose `Picture in Picture`.

Expected result: the video plays in a small window with rounded corners in the bottom right of the screen, over every app. Point at it: close and back to tab show in the top corners, back 10 seconds, play or pause, and forward 10 seconds in the middle, and the time, mute, and a progress bar you can drag at the bottom. Press and hold on the video and it plays at 2× while held, with a `2×` label at the top; let go and it plays at its old speed. Drag the video to move the window. With `Glass` on in the theme picker, the buttons turn to glass.

While a uw window is in front with its sidebar showing, the video docks at the bottom of the sidebar instead, above the foot, and keeps playing as you switch tabs. Click another uw window and it moves there. Switch to another app, or hide the sidebar with Cmd+S, and it floats in the corner of the screen; come back to uw and it docks again. Put a second video in picture in picture while one is docked: the first pauses and peeks out above the new one. Point at the docked player and the paused one slides up as a row with its still, title, and where it stopped; click it to play it again, or its × to drop it. Play a video with sound and switch to another tab: it opens in the mini player by itself, and switching back puts it in its page again. A muted or small video stays in its page. Under `Mini player` at `uw://settings/uwOrganization`, `Open the mini player when you switch tabs` turns that off, and `Float the mini player when uw is in the background` off keeps it docked while another app is in front. Under `Picture in picture`, `Don't dock` keeps a video you leave playing in its page, and one you put in picture in picture floats. `Off` turns picture in picture off: a site's picture-in-picture button does nothing.

On a site that opens its own small picture-in-picture window, such as a Google Meet call, the window docks at the bottom of the sidebar too, under any video, and keeps running when you put a video in picture in picture. Point at it for close, back to tab, and `Live`: click `Live` to turn it off for that site, and its window pauses and waits in the stack like a video whenever a video comes. The list under `Live windows` in the settings shows every site that has opened such a window. Switch to another app and it floats in its own window, with a bar in the color of the uw window it came from and the video just above it. A site that asks for a window bigger than 480 by 480 keeps floating.

Play a video or a podcast, turn off `Open the mini player when you switch tabs` or choose `Don't dock`, and switch to another tab: a row above the sidebar's foot shows what is playing, with pause, and previous and next when the site offers them. Click its title to go back to the tab.


### Hide the sidebar

Press Cmd+S, or choose View → Hide Sidebar.

Expected result: the sidebar and the window's close, minimize, and zoom buttons go, and the page fills the window inside a thin edge of the theme. Rest the pointer on the window's left edge: after a moment the sidebar slides in over the page as a card, with the window buttons at its top left, and the page does not move. Picking a tab leaves the card in. Move the pointer onto the page, or press Esc, and the card slides away. While the sidebar is hidden, the activity card shows alone over the page's bottom-left corner, with Undo. Cmd+S, or View → Show Sidebar, shows the sidebar again. Save Page As has no key.

### Terminal tabs

Choose `New Terminal` from the File menu or the new button's `▾` menu, or press Ctrl+\`.

Expected result: a tab opens with your login shell in it, named after the shell's folder. Cmd shortcuts, Ctrl+Tab, and Ctrl+\` go to uw, not the shell, and Option works as Alt. A pasted or dropped file types its quoted path. A pasted image is saved to a temporary file, and its path is typed. The organizer files a terminal tab by its title and folder, never by what the shell printed.

### Command bar

With `Cmd+T opens the command bar` on, Cmd-T, File → New Tab, and the new button's `+` open a panel over the page instead of a tab. Type a few letters of a page you visited or have open, then press Return, or use the arrow keys or the mouse to pick another row.

Expected result: the panel lists your open tabs, history, bookmarks, and a search for the typed text, each row labeled Tab, History, Bookmark, or Search. The panel is empty until you type, and nothing typed is sent to the search engine until you pick a row. Return or a click opens the row in a new tab, where Cmd-T's blank tab would have gone; a Tab row switches to that tab. Text with no row picked opens as an address, or as a search. Escape closes the panel and opens nothing, and no key typed right after Cmd-T is lost. Type part of an address you typed twice before, such as `githu`: the rest appears selected after the caret, Tab makes it typed text, and Return opens it. Type `gh` and press Tab to search GitHub, as in Chromium's address bar. Cmd-Return asks uw: a chat opens with your text as its first message, and the agent has the tab you were on as its tab 1. Option-Return runs the text as a shell command in a new terminal tab. Typing `>` first, or opening the panel with Cmd-Shift-P, marks a shell command, and Return runs it the same way. Cmd-K does nothing yet; it names the ticket that brings it. Turn the switch off and Cmd-T opens a blank tab again, in every window at once.

### New tab page

Turn off `Cmd+T opens the command bar`, then press Cmd-T. Do the same in a private window (Cmd-Shift-N).

Expected result: the tab shows uw's logo and one box, `Search or type an address`, with a Search | Ask switch, and the address bar stays empty. Nothing on the page comes from Google. Type a few letters of a page you visited: the box lists your open tabs, history, and bookmarks, as the command bar does, and Return opens the row in this tab. Tab switches to Ask: type a request and press Return, and this tab becomes a chat with your text as its first message, on the model the chip shows. Once makes it a scheduled chat. In a private window the page shows only the box and the line `A private window. uw keeps nothing from it after it closes.`, and Ask has no Once. Before any AI provider is added, Ask is grayed. The page's sections, such as Needs you and Finished, are not built yet.

### Chats

A chat asks before it changes anything on a site: a card in the chat names the step, such as `Click button "Place order"?`, with `Approve`, `Not this one`, `Stop the chat`, and a field to tell the agent what to do instead. uw asks on its own before a form's submit button, before pressing Enter in a form's field, and before typing a password or payment details, and the agent marks other changes. When Chromium's list marks a site as sensitive, the chat asks `Let the agent act on <site>?` before the agent uses it. Nothing on a page can answer for you. The agent works only while Safe Browsing is on.

The shield chip in the composer picks how much a chat asks. `Auto`, the default, asks as above. `Ask first` also asks before every click and typing, with the reason `Ask first is on.` `Full auto` never asks, not even for a sensitive site, and the chip turns amber. Expected result: a mode picked on a new chat before its first message is the mode the next new chat starts on; a mode picked later stays with that chat, also after it opens again.

While the agent works, the composer's button is Stop when the field is empty. Expected result: the waiting model request is dropped, a click or typing under way finishes, and the chat says `You stopped the chat.` Enter sends a message the agent reads after its current step; Cmd+Enter queues it as a row above the composer, sent once the agent answers. After a failure or a stop, queued rows stay, with Send now, Edit, and the ×. While a step waits for approval, Enter declines it with your message as the note.

A message can carry files of any kind: paste them, drag them onto the composer, or use the "+" menu's Add files, up to 10, at most 5 of them images. Expected result: an image shows as a tile and any other file as a card with its name, kind, and size, in the composer with an × and above your message once sent. A click on a sent one opens uw's copy in a new tab, or in Finder. uw copies each file to `~/Library/Application Support/uw/Chat files/<chat>/` and gives the agent its path, so its terminal can unzip, convert, or edit it. The model also reads text files, PDFs, and images, as image 1 and file 1 and so on, and a card says `read in part` when it got only the start. When the chat's model can't see images, as models.dev says of `deepseek-v4-pro`, the composer says `deepseek-v4-pro can't see images; the agent gets the file only.` and the message still goes. Deleting the chat deletes its folder.

Ask the agent something, then switch to another tab or app before it answers. Expected result: a macOS notification titled with the chat, saying `Answered` and the start of the answer; clicking it brings uw forward on the chat. A failure says `Didn't finish`, and a step that waits for approval says `Waits for your approval`. Nothing comes while you watch the chat, or after you press Stop. If none appears, check that uw may send them in System Settings, Notifications.

1. Open `uw://providers` and pick an `Agent model` that calls tools. Without one, chats use the language model.
2. Choose `New Chat` from the File menu or the new button's `▾` menu, or type `uw://chat`.
3. Ask for something, such as "What is the top story on Hacker News?"

A new chat asks "What should the agent do?" and suggests three requests: Tab takes the one in the field, and a click takes one of the others. With a language model on `uw://providers`, the language model writes them once a day from your recent chats and most-visited sites, and the chat after the one that asked shows them. Before that, and in a private window, the chat suggests the product's examples.

Expected result: the chat tab takes your first message as its title, and the chat's heading shows it with what the chat is doing: `Working`, then minutes, or `Waits for your approval`. The pages the agent opens appear as child tabs under the chat tab and load in the background, and your selected tab stays selected. Each step shows as a quiet line with what it acted on in bold, such as `Clicked button "Send" in tab 1`, then the answer. The agent may fill in several fields and submit in one go; if a step fails or you decline it, the rest of that go does not run. When the model request fails, `Try again` asks once more without a new message, and `AI providers` opens `uw://providers`. The agent reads pages as text, and it clicks and types only on https pages. With automatic organization on, the organizer may still move the agent's tabs out from under the chat.

Each chat runs on its own model. Its composer has the model chip and, for a model with levels, the effort chip; a new chat starts on the agent model. The model menu groups models by provider, dims the ones that call no tools, and ends with `Manage providers`. Expected result: a switch applies from the next request, even while the agent works, and a thin line such as `Now gpt-5 · high` marks it. The chat keeps its model when it opens again. A chat whose provider was removed, or whose model the provider no longer lists, says so and sends nothing until you pick another model.

Chats are saved after every step. Quit uw in the middle of one and start it again with "Continue where you left off" set at `chrome://settings/onStartup`. Expected result: the chat tab comes back with everything it showed and the line `Stopped before it finished` with `Continue`. Continue goes on in the same agent tabs.

The chat page lists every saved chat on its left, the one you last wrote to first, with a spinner while one runs, a pause sign when it stopped early, and a filled dot for an answer you have not seen. Click a chat to go to its tab, or to reopen it in a new tab once its tab is closed. `New chat` opens another, the search field filters by title, and the bin at the end of a row deletes the chat and closes its tab.

On the right, a card for each of the agent's tabs shows a picture of the tab after the agent's last step there and what it did. Click a card to go to its tab. `Side by side` puts the chat and the agent's tab next to each other in split view, and the split follows the agent to its other tabs. The sidebar shows the pair as one row in the chat's place, with the agent's other tabs under it. Click it again to end the split.

`Skills` at the foot of the chat list shows every skill the agent can use, from uw's `skills` folder in `~/Library/Application Support/uw-dev` and from `~/.agents/skills`, each with a switch. `Open skills folder` shows uw's folder in Finder. The agent reads a skill's instructions when a request matches its description, and the composer then says which skill it uses. Ask a chat to "make this a skill": a `Save a skill?` card appears, and nothing is written until you click `Save skill`.

### The uw command

Open `uw://settings/system` and click `Install uw command` at the top. macOS asks for your Mac's password once.

Expected result: the row says `Installed at /usr/local/bin/uw`, with `Uninstall`. In a terminal, `uw settings list` prints uw's settings, each with its value, what it takes, and the workspaces that share it. `uw settings get organizer.enabled` prints `true` or `false`, and `uw settings set theme.grain 30 --workspace Work` changes that workspace's grain at once. A wrong value, such as `uw settings set theme.grain 140`, says what the setting takes. Quit uw and run `uw settings list` again: uw starts with no window and answers. `uw --help` and `uw settings --help` show examples. `Uninstall` removes the command, and uw stops listening for it.

`uw mcp add --transport http linear https://mcp.linear.app/mcp` adds Linear, as `claude mcp add` would, and opens its sign-in tab in uw. `uw mcp list` shows each connector and what it needs.

`uw chat -p "What's the newest post on Hacker News?"` runs uw's agent out of sight: its steps print as it works, and its answer prints at the end. The chat shows in uw's chat list with a terminal icon. Try `--mode ask-first`: a step that needs approval asks in the terminal, and typing `1` approves it. Pipe it into `cat` (`uw chat -p "…" | cat`) and the same step makes the command exit with 3 and print `uw approve <id> allow`; run that, then `uw chat -r <id> -p` to carry on. Ctrl-C stops the chat. `uw chats` lists the workspace's chats.

`uw chat` alone opens the terminal interface. Type a request and press Enter: the steps tick off as the agent works, and the answer is drawn as Markdown. Press Enter while it works to steer it, and Esc to stop it. Type `/` for skills and connector prompts. A step that needs approval shows a card: press a number, or type what to do instead and press Enter. Ctrl-O moves the chat and its tabs into your front uw window. Ctrl-C leaves and prints how to carry on.

`claude mcp add uw -- uw mcp serve` gives Claude Code uw's page tools. Ask Claude Code to open and read a page: its tab opens out of sight, and none shows in your windows. Ask it to fill in and send a form: Claude Code shows uw's question, and the tab waits, unselected, at the end of your front window. Not this one, with a note, keeps the step from happening and tells the agent why. Add `--mode ask-first` to be asked before every click and typing. A client that can't ask, such as Claude's desktop app today, gets uw's notification with Approve and Not this one instead.

A build started with `pnpm run launch` keeps its data in `uw-dev`, so point the command there: `UW_USER_DATA_DIR="$HOME/Library/Application Support/uw-dev" uw settings list`.

## Not available yet

- The last details of the Arc-style look: the shadow around the page card and the slimmer toolbar.
- [Themes](plans/themes.md): the picker, themes of more than one color, grain, and black dark mode.
- The brief tint on a tab that was just moved.
- Shared pinned-page sessions, saved home URLs, and Peek previews.
- The Chats section in the sidebar, scheduled chats, and prompt-built site customizations.
- A warning before a terminal that is running a command closes, and terminals that come back in their folder after a restart.
- Verified extension compatibility or daily-use stability.

## Report a problem

Help → Report an Issue… and Settings › Privacy and security › Report an issue open a new GitHub issue with uw's version filled in. A release also asks once, at the foot of the sidebar, whether it may send crash and problem reports; a build made from source never asks and sends nothing ([Problem reports](plans/distribution.md#problem-reports)).

Include the version reported by `pnpm run smoke`, your macOS version, whether the profile was new or existing, the exact steps, the expected result, and the actual result. A screenshot or a short screen recording helps for layout, dragging, focus, and restoration problems.

For a crash, also record the quit time, then attach the newest crash report:

```sh
ls -lt ~/Library/Logs/DiagnosticReports/uw*.ips | head -3
```

See the [native tab tree acceptance checks](plans/native-tab-tree.md#acceptance-checks) for the complete engineering verification list.
