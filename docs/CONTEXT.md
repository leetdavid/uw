# uw browser terminology

Terms used in the product docs.

## Language

**Browsing task**:
The immediate activity the user is pursuing with a set of tabs. A broader project can contain several browsing tasks; this is the level of user intent that tab organization tries to infer.

**Tab group**:
A native Chromium tab group holding the regular tabs of one browsing task in one window. Tab groups do not nest. Also called a task.
_Avoid_: Workspace, space.

**Pinned tab**:
A persistent sidebar icon with a saved home URL. When open, it represents one continuing live page session shared across windows in its browser profile.

**Inactive pinned view**:
A hazed placeholder where a pinned page's live session is currently displayed in another window. Selecting it transfers the session back.

**Regular tab**:
A page tab or terminal tab in a window's tab tree.

**Terminal tab**:
A regular tab that shows a shell instead of a web page. The organizer never moves it and sends nothing about it to a model.
_Avoid_: Console, shell tab.

**Busy terminal**:
A terminal tab whose shell is running a command.

**uw page**:
A page that is part of the browser, such as settings. Its address starts with `uw://`.
_Avoid_: Chrome page, internal page, WebUI page.

**Home URL**:
The saved starting address a pinned tab returns to when reset.

**Pinned site**:
The scheme and registrable domain of a pin's home URL. Navigation inside that site stays in the pin.

**Peek preview**:
A temporary view opened from a pinned tab for a page outside the pin's site. Promoting it creates a regular tab in Scratchpad.

**Tab tree**:
The arrangement of regular tabs within one browser window: the Scratchpad, the tab groups, and regular tabs nested under parent tabs.

**Tab branch**:
A tab together with all its descendant tabs.

**Parent tab**:
A regular tab with child tabs beneath it in the tab tree.

**Selected tab**:
The tab whose page is currently displayed in a browser window.

**Ungrouped tab**:
A tab currently outside all tab groups. Its relationship to a browsing task may still be unclear.

**Scratchpad**:
The always-visible section holding ungrouped regular tabs and candidate branches in one window. It is not a browsing task or tab group.

**Manual adjustment**:
A user-made change to tab grouping, ordering, or a task group's name.

**Task extraction**:
Separating a tab branch or selected tabs into a task group of their own.

**Organization action**:
A set of related changes to the tab tree, such as creating a task group and moving tabs into it, treated as one operation.

**Task proposal**:
The organizer's suggestion that a set of tabs represents a separate browsing task.

**Classification call**:
One LLM request about where a tab belongs. It says which task the tab belongs to, or none, with a confidence. For a tab outside every task it says how likely the tab starts a task of its own, and supplies the name of that task. uw decides about a tab branch from the answers about its tabs.

**Provisional task name**:
The `New task` title a task has until the language model returns a valid name for it. The organizer does not offer such a task to a model as an answer.

**Decision model**:
A model that answers a typed question with a choice and a confidence instead of text. uw can use Jev, the OpenAI Decisions API, or a compatible endpoint to decide a tab branch's placement and whether a task needs a name. It does not write the name.

**Language model**:
The chat model uw connects to through Ollama or an OpenAI-compatible endpoint. It names new tasks, and decides placement when no decision model is set.

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
Facts uw's agent has learned about the user, such as which bank they use. The user can read, edit, and clear them.

**Approval**:
The user's go-ahead for one change an agent wants to make on a site, such as submitting a form, buying, sending, deleting, cancelling, changing account settings, or signing in with a saved password or passkey. Reading, searching, navigating, and filling in fields need none.

**Outside agent**:
An AI agent running outside uw, such as Claude Code or Codex, that the user lets work in uw's tabs.

**Site customization**:
A user-requested change to a website's appearance or behavior.
