# uw browser terminology

Terms used in the product docs.

## Language

**Browsing task**:
The immediate activity the user is pursuing with a set of tabs. A broader project can contain several browsing tasks; this is the level of user intent that tab organization tries to infer.

**Task**:
The regular tabs of one browsing task in one window, shown as a folder in the sidebar. Tasks do not nest. uw says "task" wherever it shows one, in menus, tooltips, and VoiceOver. Underneath, a task is a Chromium tab group; say "tab group" only for that mechanism.
_Avoid_: Group, tab group, task group, workspace, space.

**Workspace**:
A named area of browsing that the user swipes between, as Arc's spaces are, such as work and personal. Each belongs to one profile; tasks organize the tabs inside it.
_Avoid_: Space.

**Profile**:
The logins, cookies, history, and extensions that one or more workspaces share, as in Arc's profiles.
_Avoid_: Account, user.

**Theme**:
The colors uw draws its window in: the tint behind the sidebar and the page card. A profile has one theme, shared by all its windows.
_Avoid_: Skin, space color.

**Window mode**:
Whether uw's window is light or dark: the sidebar, menus, and uw's panels such as the command bar and the theme picker. Auto follows macOS. Chromium's own pages, such as Settings, follow it too. The toolbar takes the page color instead.
_Avoid_: Skin, browser mode, color scheme.

**Page mode**:
Whether pages in tabs are asked for their light or dark look: websites and uw's own tab pages, such as the chat page. It is the window mode unless the user picks Light or Dark for pages. A site with no dark look of its own stays light.
_Avoid_: Website theme, content theme, web dark mode.

**Page color**:
The color a page paints behind its content. The toolbar above the page and the bookmarks bar take it, whatever the window mode and page mode, and fade when it changes.
_Avoid_: Theme color, site color, tint.

**Pinned tab**:
A persistent sidebar icon with a saved home URL. When open, it represents one continuing live page session shared across windows in its browser profile.

**Inactive pinned view**:
A hazed placeholder where a pinned page's live session is currently displayed in another window. Selecting it transfers the session back.

**Regular tab**:
A page tab or terminal tab in a window's tab tree.

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
The arrangement of regular tabs within one browser window: the Scratchpad, the tasks, and regular tabs nested under parent tabs.

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
The always-visible section holding ungrouped regular tabs and candidate branches in one window. It is not a task.

**Manual adjustment**:
A user-made change to tab grouping, ordering, or a task's name.

**Task extraction**:
Separating a tab branch or selected tabs into a task of their own.

**Organization action**:
A set of related changes to the tab tree, such as creating a task and moving tabs into it, treated as one operation.

**Task proposal**:
The organizer's suggestion that a set of tabs represents a separate browsing task.

**Classification call**:
One LLM request about where a tab belongs. It says which task the tab belongs to, or none, with a confidence. For a tab outside every task it says how likely the tab starts a task of its own, and supplies the name of that task. uw decides about a tab branch from the answers about its tabs.

**Provisional task name**:
The `New task` title a task has until the language model returns a valid name for it. The organizer does not offer such a task to a model as an answer.

**Provider**:
A company or address that serves models, such as OpenAI, Anthropic, Jev, or a local Ollama, saved once in settings with its key. One provider can serve both language models and decision models.
_Avoid_: Connection, endpoint, backend

**Proxy**:
A provider that serves other providers' models at its own address, such as CLIProxyAPI, OpenRouter, or a company gateway. A network proxy that carries all of the browser's traffic is not one.
_Avoid_: Gateway, router, relay

**Decision model**:
A model from a provider that answers a typed question with a choice and a confidence instead of text, such as Jev or a model behind the OpenAI Decisions API. It decides a tab branch's placement and whether a task needs a name. It does not write the name.

**Language model**:
A model from a provider that answers in text. One names new tasks, and decides placement when no decision model is set.

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
The question in a placement request that asks whether a tab is the same piece of work as the tabs that make up its task. A tab that is, and yet fits no task name, shows that its task has outgrown its name. It is asked only about a generated name.

**Generated name**:
A task name uw gave. uw may replace it, and remembers it across a restart. A name the user typed is not generated and is never replaced, until the user hands it back with the name lock or asks for a new one with "Suggest a name" in the task's menu.

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
A chat uw starts again on a schedule, such as every morning at 9, to check pages or carry out a request. Watching a page for changes is one use.
_Avoid_: Page watch, routine.

**Catch-up run**:
A single run of a scheduled chat after uw becomes available following missed scheduled runs.
_Avoid_: Catch-up check.

**Condition-based alert**:
A notification that a scheduled chat found a result matching the user's condition.

**Scheduled summary**:
A report from one or more scheduled chats, delivered on a chosen schedule.

**Chat**:
A conversation with uw's built-in agent, which may browse for the user to answer or carry out a request. Its page is a regular tab, and the tabs the agent opens are child tabs beneath it.
_Avoid_: Delegated browser task, agent task, run, job.

**Private chat**:
A chat started from a private window. Nothing from it is kept after the private windows close.

**Agent memory**:
What uw's agent keeps between chats: plain sentences about the user, one a memory, such as which bank they use. The agent reads them all at the start of a request and keeps or forgets them as it works, without asking; the user rewords, forgets, and puts them back.
_Avoid_: Notes, knowledge base, context

**Skill**:
Instructions that teach uw's agent how to do one kind of request, with a name and a one-line description. The agent sees every skill's description and reads a skill's instructions only when a request calls for it. Unlike agent memory, a skill says how to do something, not what is true about the user.
_Avoid_: Prompt, routine, workflow.

**Approval**:
The user's go-ahead for one change an agent wants to make on a site, such as submitting a form, buying, sending, deleting, cancelling, changing account settings, or signing in with a saved password or passkey. Reading, searching, navigating, and filling in fields need none.

**Approval mode**:
How much a chat asks before it acts. Ask first asks before every click and typing. Auto, the default, asks before each change on a site and each sign-in. Full auto never asks. A chat keeps its own mode; a mode picked on a new chat before its first message becomes the default for new chats.
_Avoid_: permission mode, autonomy level.

**Steer**:
To send a chat a message while its agent works. The agent reads it after its current step, before it asks the model again. A queued message instead waits until the agent has answered.
_Avoid_: interrupt, inject.

**Outside agent**:
An AI agent running outside uw, such as Claude Code or Codex, that the user lets work in uw's tabs.

**Site customization**:
A user-requested change to a website's appearance or behavior.
