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
A page tab in a window's task tree.

**Home URL**:
The saved starting address a pinned tab returns to when reset.

**Pinned site**:
The scheme and registrable domain of a pin's home URL. Navigation inside that site stays in the pin.

**Peek preview**:
A temporary view opened from a pinned tab for a page outside the pin's site. Promoting it creates a regular tab in Scratchpad.

**Tab tree**:
The arrangement of regular tabs within one browser window: the Scratchpad, the tab groups, and page tabs nested under parent tabs.

**Tab branch**:
A tab together with all its descendant tabs.

**Parent tab**:
A page tab with child tabs beneath it in the tab tree.

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
One LLM request that decides a tab branch's placement. It says which task the branch belongs to, or none, with a confidence. For a branch outside every task it says how likely the branch starts a task of its own, and supplies the name of that task.

**Provisional task name**:
The `New task` title a newly created task keeps when its classification call returns no valid name.

**Decision model**:
A model that answers a typed question with a choice and a confidence instead of text. uw can use Jev, the OpenAI Decisions API, or a compatible endpoint to decide a tab branch's placement. It does not name tasks.

**Language model**:
The chat model uw connects to through Ollama or an OpenAI-compatible endpoint. It names new tasks, and decides placement when no decision model is set.

**Task naming call**:
A language-model request that names a task the decision model started.

**User-directed mode**:
An organization mode in which the user accepts or rejects task proposals while routine tab placement remains automatic.

**Organization pause**:
A global suspension of automatic tab placement and task proposals. Manual organization and explicitly requested reorganization remain available.

**Page watch**:
A scheduled check of a page for a user-defined condition or relevant updates.

**Catch-up check**:
A single page-watch run after uw becomes available following missed scheduled checks.

**Condition-based alert**:
A notification that a page watch found a result matching the user's condition.

**Scheduled summary**:
A report of watched-page updates delivered on a chosen schedule.

**Delegated browser task**:
A browser automation job the user asks an agent to carry out, such as retrieving information or taking an action.

**Site customization**:
A user-requested change to a website's appearance or behavior.
