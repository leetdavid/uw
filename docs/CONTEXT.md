# uw browser terminology

Terms used in the product docs.

## Language

**Browsing task**:
The immediate activity the user is pursuing with a set of tabs. A broader project can contain several browsing tasks; this is the level of user intent that tab organization tries to infer.

**Task**:
The regular tabs of one browsing task in one workspace, shown as a folder in the sidebar. Tasks do not nest. uw says "task" wherever it shows one, in menus, tooltips, and VoiceOver. Underneath, a task is a Chromium tab group; say "tab group" only for that mechanism.
_Avoid_: Group, tab group, task group, workspace, space.

**Workspace**:
A named area of browsing that the user swipes between, as Arc's spaces are, such as work and personal. Each belongs to one profile; tasks organize the tabs inside it.
_Avoid_: Space.

**Profile**:
The logins, cookies, history, and extensions that one or more workspaces share, as in Arc's profiles.
_Avoid_: Account, user.

**Theme**:
The colors uw draws its sidebar and the edge around the page card in, and whether they are light or dark. Each workspace has its own theme, so two workspaces on one profile can look different.
_Avoid_: Skin, space color, window mode.

**Page mode**:
Whether everything outside the sidebar is light or dark: websites, Chromium's and uw's pages, and Chromium's menus, bubbles, and dialogs. A site with no dark look of its own stays light.
_Avoid_: Page theme, website theme, content theme, browser mode, web dark mode.

**Page color**:
The color a page paints behind its content. The toolbar above the page and the bookmarks bar take it, whatever the theme and page mode, and fade when it changes.
_Avoid_: Theme color, site color, tint.

**Transparency**:
A theme setting, from solid to clear, that lets the desktop behind uw's window show through, blurred: through the sidebar and the edges around the page under the theme, and through the toolbar under the page color. The page stays solid; it does not scroll under the toolbar.
_Avoid_: Glass, translucency, opacity, vibrancy.

**Glass**:
A theme setting, from flat to clear, that turns the window's pin tiles, active tab, and buttons into lit glass, as macOS's Liquid Glass does its controls. It is apart from transparency.
_Avoid_: Transparency, blur.

**Hidden sidebar**:
A sidebar the user has hidden, as Arc's Hide Sidebar does. It takes no width, and the page fills the window.
_Avoid_: Collapsed sidebar, narrow sidebar.

**Floating sidebar**:
A hidden sidebar shown over the page for a moment, as in Arc. The page stays where it is underneath.
_Avoid_: Peek, overlay, expanded sidebar.

**Mini player**:
A video playing outside its page, as in Arc: docked at the bottom of the sidebar, or floating in its own window. Underneath it is Chromium's picture in picture; say "picture in picture" only for that mechanism.
_Avoid_: PiP, sidebar player, video overlay.

**Mini player stack**:
Up to three mini players docked at the bottom of the sidebar. The top one plays; the ones under it are paused until the user picks one.
_Avoid_: Queue, deck.

**Floating mini player**:
A mini player in its own window over other apps, shown while a docked one would be hidden: uw's window covered by another app's or not showing, or the sidebar hidden.
_Avoid_: PiP window, pop-out.

**Live window**:
A site's own picture-in-picture window, such as a Meet call, that keeps running while videos come and go: it never pauses and never joins the mini player stack. Docked, it sits below any mini player. Every site's own window is live unless the user turns that off for its site; then it acts like a mini player, and pauses and joins the stack when another one comes.
_Avoid_: Call window, live mini player, document PiP.

**Playback controls**:
Play, pause, and skip for a tab the user is not looking at that plays sound, shown at the bottom of the sidebar. Sound that is in a mini player or a live window has its own controls instead.
_Avoid_: Media controls, now playing, audio bar.

**Pinned tab**:
A persistent sidebar icon with a saved home URL. When open, it represents one continuing live page session shared by every window and workspace of its profile.

**Inactive pinned view**:
A hazed placeholder where a pinned page's live session is currently displayed in another window. Selecting it transfers the session back.

**Regular tab**:
A page tab or terminal tab in a workspace's tab tree.

**Integration**:
A tool from the user's computer that uw brings into the browser, such as the shell in a terminal tab.
_Avoid_: Plugin, extension.

**Terminal tab**:
A regular tab that shows a shell instead of a web page.
_Avoid_: Console, shell tab.

**Busy terminal**:
A terminal tab whose shell is running a command.

**uw page**:
A page that is part of the browser, such as settings. Its address starts with `uw://`.
_Avoid_: Chrome page, internal page, WebUI page.

**New tab page**:
The uw page in a blank tab: a box to search or ask, and the state of uw's features, such as steps waiting for approval, finished chats and runs, and what runs next.
_Avoid_: Dashboard, home, status page, NTP.

**Command bar**:
uw's popup for opening pages and running commands from the keyboard. It searches commands, tasks, open tabs, history, bookmarks, and the web, and can send what was typed to a chat or a terminal.
_Avoid_: Palette, command palette, launcher, omnibox.

**Result menu**:
The list of things the user can do with one command bar result, such as switching to a tab, closing it, or copying its link.
_Avoid_: Action panel, actions.

**Home URL**:
The saved starting address a pinned tab returns to when reset.

**Pinned site**:
The scheme and registrable domain of a pin's home URL. Navigation inside that site stays in the pin.

**Peek preview**:
A temporary view opened from a pinned tab for a page outside the pin's site. Promoting it creates a regular tab in Scratchpad.

**Tab tree**:
The arrangement of regular tabs within one workspace, shown the same in every window on it: the Scratchpad, the tasks, and regular tabs nested under parent tabs.

**Tab branch**:
A tab together with all its descendant tabs. It is the only level below a task.
_Avoid_: Subtask, subgroup, nested task.

**Parent tab**:
A regular tab with child tabs beneath it in the tab tree.

**Child tab**:
A regular tab nested directly under a parent tab, in its parent's tab branch and task. A child the user nested by hand stays with its parent; a child opened from a link can leave for another task.

**Selected tab**:
The tab whose page is currently displayed in a browser window.

**Ungrouped tab**:
A tab currently outside every task, so in the Scratchpad. Its relationship to a browsing task may still be unclear.

**Scratchpad**:
The always-visible section holding ungrouped regular tabs and candidate branches in one workspace. It is not a task.

**Manual adjustment**:
A user-made change to tab grouping, ordering, or a task's name.

**Kept tab**:
A tab the user told uw to leave where it is, in its task or in the Scratchpad. The organizer does not ask about it or move it until the user lets it go or moves it to another task or to the Scratchpad.
_Avoid_: Pinned tab, locked tab, held tab.

**Task extraction**:
Separating a tab branch or selected tabs into a task of their own.

**Tidy**:
Filing the Scratchpad's tabs into tasks all at once, when the user asks. Tabs that fit no task and start none stay in the Scratchpad.
_Avoid_: Reorganize all tabs, organize all, sort.

**Clear**:
Closing the Scratchpad's tabs all at once, except kept tabs and busy terminals.
_Avoid_: Archive, close all.

**Organization action**:
A set of related changes to the tab tree, such as creating a task and moving tabs into it, treated as one operation.

**Task proposal**:
The organizer's suggestion that a set of tabs represents a separate browsing task.

**Classification call**:
One LLM request about where a tab belongs. It says which task the tab belongs to, or none, with a confidence. For a tab outside every task it says how likely the tab starts a task of its own, and supplies the name of that task. uw decides about a tab branch from the answers about its tabs.

**Provisional task name**:
The `New task` title a task has until the language model returns a valid name for it. The organizer does not offer such a task to a model as an answer.

**Provider**:
A company or address that serves models, such as OpenAI, Anthropic, Jev, or a local Ollama, saved once in settings with its key, for every profile. One provider can serve both language models and decision models.
_Avoid_: Connection, endpoint, backend

**Proxy**:
A provider that serves other providers' models at its own address, such as CLIProxyAPI, OpenRouter, or a company gateway. A network proxy that carries all of the browser's traffic is not one.
_Avoid_: Gateway, router, relay

**Decision model**:
A model from a provider that answers a typed question with a choice and a confidence instead of text, such as Jev or a model behind the OpenAI Decisions API. It is the usual quick-choice model. It never writes text, such as a task's name.
_Avoid_: Evaluation model, classifier

**Language model**:
A model from a provider that answers in text. One names new tasks, and it is the quick-choice model unless another is picked.

**Quick-choice model**:
The model uw asks for its quick choices: where a tab branch goes, whether a task needs a name, the command check, and the memory check. It is a decision model, or a language model, by default the one that names tasks.
_Avoid_: Decision slot, classifier model

**Agent model**:
The language model a new chat starts on. Each chat can switch to another.

**Effort**:
How long a model may think before it answers, in the levels its provider offers for that model.
_Avoid_: Reasoning level, thinking budget

**Model catalog**:
The public list at models.dev of what each provider's models can do: whether a model calls tools, and its effort levels. uw keeps a copy and refreshes it daily.

**Task naming call**:
A language-model request for a task name. It is sent only when the placement answer calls for a name: for a new task, for a task with the provisional name that a tab starts, and for a task that has outgrown the name uw gave it. After a call that failed or gave no usable name, it is sent again for the next tab that changes and stays in that task.

**Name check**:
The question in a placement request that asks whether a tab is the same piece of work as the other tabs of its task. A tab leaves its task only when the answer is that it is not. A tab that is, and yet fits no task name, shows that its task has outgrown a generated name.

**Generated name**:
A task name uw gave. uw may replace it, and remembers it across a restart. A name the user typed is not generated and is never replaced, until the user hands it back with the name lock or asks for a new one with "Name Task".

**Name lock**:
The small lock on the sidebar row of a task whose name the user typed. A click hands the name back to uw, which then treats it as a generated name.

**Hub**:
A page the user comes back to for many things, such as a home page, a feed, an inbox, or a dashboard. uw never makes a hub into a task.

**Threshold**:
How sure a model must be before uw acts on one kind of answer without asking. There are three, each set by the user: to move a tab branch, to start a task, and to rename a task.
_Avoid_: Eagerness, aggressiveness, sensitivity.

**User-directed mode**:
An organization mode in which the user accepts or rejects task proposals while routine tab placement remains automatic.

**Organization pause**:
A global suspension of automatic tab placement and task proposals. Manual organization and explicitly requested reorganization remain available.

**Scheduled chat**:
A chat uw runs again on its own, on a schedule such as every morning at 9 or on a trigger, to carry out the same request, such as checking a price or looking for news. It need not be about one page.
_Avoid_: Page watch, watch, routine.

**Run**:
One time a scheduled chat carries out its request, out of sight, shown as one message in the chat. Underneath, a run is a subagent; say "subagent" only for that mechanism.
_Avoid_: Check, job, execution.

**Check now**:
The button that starts a run of a scheduled chat at once, outside its schedule. Its label is the one place "check" stands for a run.
_Avoid_: Run now, refresh.

**Trigger**:
A command left running out of sight that wakes its chat once when it ends, or once for each line it prints. In a scheduled chat each wake starts a run.
_Avoid_: Monitor, watcher, hook.

**Tracked value**:
A value a run records each time, such as a price or whether something is in stock, kept so uw can show how it changed over time.
_Avoid_: Metric, data point.

**Watched page**:
A page that a scheduled chat checks. The page shows that it is watched. The user starts one from the page itself, or by asking in a chat.
_Avoid_: Page watch, tracked page.

**Catch-up run**:
A single run of a scheduled chat after uw becomes available following missed scheduled runs.
_Avoid_: Catch-up check.

**Change alert**:
A notification that a run of a scheduled chat found what the user asked to hear about, such as a lower price or a new story.
_Avoid_: Condition-based alert.

**Regular update**:
A notification after every run of a scheduled chat, saying what the run found.
_Avoid_: Scheduled summary, digest.

**Alert**:
A notification from a scheduled chat, kept in uw, such as a change alert, a regular update, a failed run, or a step waiting for approval. Notifications from other chats are not alerts.
_Avoid_: Notification, when meaning the kept copy.

**Inbox**:
The list of alerts, opened from a button at the top of the sidebar.
_Avoid_: Notification center, alerts panel.

**Chat**:
A conversation with uw's built-in agent, which may browse for the user to answer or carry out a request. Its page is a regular tab, and the tabs the agent opens are child tabs beneath it.
_Avoid_: Delegated browser task, agent task, run, job.

**Agent's tab**:
A tab that a chat's agent opened, nested under the chat. It is the only kind of tab the agent reads or acts in; the user's other tabs it can only arrange.
_Avoid_: Agent tab.

**Private chat**:
A chat started from a private window. Nothing from it is kept after the private windows close.

**Attached material**:
Information the user hands the agent with a message: an attached file, or text piped into the `uw` command. Like page text, it is never the user's instruction.
_Avoid_: Context, input

**Attached file**:
A file the user adds to a chat message. uw keeps a copy in the chat's own folder, where the agent's terminal can open it, and the model also reads it when it can.
_Avoid_: Attachment, upload, document

**Agent memory**:
What uw's agent keeps between chats: plain sentences about the user, one a memory, such as which bank they use. Each profile has its own. The agent keeps and forgets them as it works, without asking; the user rewords and forgets them.
_Avoid_: Notes, knowledge base, context

**Core memory**:
A memory the agent reads with every request, because it is useful almost always, such as the user's name or city. The agent searches the other memories when a request calls for them.
_Avoid_: Pinned memory, profile, summary

**Memory check**:
A quick model call uw makes before the agent keeps a memory, to judge whether the user said or clearly meant it and whether it holds a secret. It sees the user's own messages, never page text, attached material, or tool results. An unsure or missing answer keeps nothing.
_Avoid_: Memory filter, memory guard

**Skill**:
Instructions that teach uw's agent how to do one kind of request, with a name and a one-line description. The agent sees every skill's description and reads a skill's instructions only when a request calls for it, or when the user starts the skill by typing `/` and its name. A skill can be for the user only, which the agent never sees, or for the agent only. Unlike agent memory, a skill says how to do something, not what is true about the user.
_Avoid_: Prompt, routine, workflow.

**Connector**:
A program on this Mac, or a web address, that gives uw's agent more tools. uw says "connector" wherever it shows one. Underneath, a connector is an MCP server; say "MCP server" only for that mechanism.
_Avoid_: Integration, plugin, tool server.

**Recommended connector**:
A service on uw's own short list of connectors that uw has tested: its web address is the service's own, and uw signed in to it and listed its tools with a real account.
_Avoid_: Verified, official, featured.

**Help**:
uw's how-to pages for the people who use it, published at uwbrowser.com/help. They describe only what is built. uw's agent reads them through the uw Help connector, which comes with uw and can be turned off but not removed.
_Avoid_: Docs, documentation, user guide, manual, design docs.

**Plugin**:
A package a service publishes that adds its connector, its skills, or both to uw in one step.
_Avoid_: App, extension, integration.

**Approval**:
The user's go-ahead for one change an agent wants to make on a site, such as submitting a form, buying, sending, deleting, cancelling, changing account settings, or signing in with a saved password or passkey. Reading, searching, navigating, and filling in fields need none. In a terminal, an approval lets one command run, or one set of keys reach a running program. For a connector, it lets one tool call run, or every call of that tool for the rest of the chat or for good. For the user's own tabs, it lets one call close the tabs it names.

**Approval mode**:
How much a chat asks before it acts. Ask first asks before every click, typing, terminal command, connector tool call, and change to the user's tabs. Auto, the default, asks before each change on a site, each sign-in, each command the command check finds dangerous, each connector tool not marked as only reading, and each closing of the user's tabs. Full auto never asks, except before a command that may read a sensitive file, which every mode asks about. A chat keeps its own mode; a mode picked on a new chat before its first message becomes the default for new chats.
_Avoid_: permission mode, autonomy level.

**Command check**:
A quick model call uw makes before the agent runs a terminal command or sends keys to a running program, to judge whether it is potentially dangerous and whether it may read a sensitive file. It sees the user's messages and the agent's actions, never page text or command output. An unsure or missing answer counts as dangerous.
_Avoid_: Classifier, guard, safety check.

**Sensitive file**:
A file that holds secrets, such as a `.env` file, a key or certificate, a password store, or the credentials of a tool such as ssh, AWS, or gcloud. The agent reads one only through a command the user approved, in every approval mode.
_Avoid_: Secret file, private file.

**Password prompt**:
A program in a terminal tab waiting for the user to type a password or passphrase, such as `sudo`, `ssh`, or `sudo` on another machine through `ssh`. uw hands it to the user, and the agent never types there.
_Avoid_: Secret prompt, credential prompt.

**Steer**:
To send a chat a message while its agent works. The agent reads it after its current step, before it asks the model again. A queued message instead waits until the agent has answered.
_Avoid_: interrupt, inject.

**Outside agent**:
An AI agent running outside uw, such as Claude Code or Codex, that the user lets work in uw's tabs.

**`uw` command**:
What the user types in a terminal, or a script or outside agent runs, to start chats, arrange tabs, manage connectors, and change settings in uw. It works only once the user installs it.
_Avoid_: CLI, command-line tool, terminal app.

**Site customization**:
A user-requested change to a website's appearance or behavior.

**Preview**:
A uw release anyone can download from GitHub or the website, marked as rough, with no support promise. Each has its own uw version, such as 0.1.0, and updates itself.
_Avoid_: Test build, beta, nightly.

**Problem report**:
What uw sends, only with the user's consent, to help fix it: a native crash report, or a rage click, dead click, or JavaScript error on one of uw's own pages. Never anything from a website the user visits.
_Avoid_: Telemetry, analytics, tracking.
