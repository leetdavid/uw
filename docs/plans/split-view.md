# Split views

Status: scope agreed on 2026-11-04 in a grilling session, after the user asked for "more than just 2-tab splitting ... as many as the user wants like a terminal window splitting", with drag and drop to rearrange the panes, Cmd+D to split left and right, and Cmd+Shift+D to split top and bottom. The design, T331, was drawn the same day and waits for the user's sign-off. Nothing is built.

Implementability: **3/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium's split view has the drop zones, mini toolbars, restore, and sidebar handling, but holds exactly two tabs, and about 80 of its files read splits. uw patches it to hold any number ([ADR 0029](../adr/0029-split-views-extend-chromiums-split.md)), and each Chromium update can conflict. Pinned split views are the hardest part: a live pin moves one page between windows today, and this moves several pages and their layout.

Today a [split view](../CONTEXT.md#language) is Chromium's: two tabs, side by side or stacked, drawn as one box in the sidebar per [Side by side in the sidebar](keyboard-and-mouse.md#side-by-side-in-the-sidebar). The agent's Side by side (T71) uses it.

## Agreed scope

### Panes

- A split view shows tabs together in one window's page area, each in its own [pane](../CONTEXT.md#language). Each pane is a tab, so the command bar, tab search, the organizer, the agent's tab tools, and restore each see it as one.
- Any kind of tab can fill a pane: a web page, a terminal, a chat, or a uw page.
- Splitting halves the focused pane, side by side or one above the other. Any pane can be split again either way, as in a terminal. Every divider drags.
- A split has no limit on its panes. A pane is never narrower or shorter than 200px: a split or a drop that would make one smaller does nothing, and its drop zone does not light. A shrinking window squeezes the panes by Chromium's rule, to 10% of the space and never under 50px.
- The pane with focus holds the [selected tab](../CONTEXT.md#language).

### Splitting

- Split Right, Cmd+D, splits the focused pane left and right, with the new pane on the right. Split Down, Cmd+Shift+D, splits it top and bottom, with the new pane below. Both are in the Tab menu and a tab's right-click menu.
- The new pane gets what Cmd+T gives: the command bar when it is on, or the new tab page. Picking an open tab in the command bar moves that tab into the pane.
- From a terminal tab, the new pane is a new terminal in the same folder. A chat gets what Cmd+T gives.
- The new tab is in the split's task, or in the Scratchpad with it.
- The new button's "New Split View" becomes "Split Right", with Cmd+D.

### Keys

- Bookmark This Tab and Bookmark All Tabs lose Cmd+D and Cmd+Shift+D. They keep their menu entries with no key, and the user can give them one in [Keyboard shortcuts](keyboard-and-mouse.md#keyboard-shortcuts).
- Cmd+Option+N, today's New Split View, goes.
- A page that handles Cmd+D itself keeps it, as with every uw key.
- Ctrl+Tab and Ctrl+Shift+Tab step through the panes in reading order, then on to the next tab, since each pane is a tab. No key moves focus by direction. Cmd+Option+← and → keep switching workspace; the grilling named them by mistake on 2026-11-04.
- Windows uses Ctrl+D and Ctrl+Shift+D.

### Closing and ending

- Cmd+W, or the × on a pane's mini toolbar, closes that pane's tab. The panes beside it take its space. With one pane left, the split ends.
- "Move Out of Split View" keeps the tab open and puts it just below the split in the sidebar.
- "Separate Panes" ends the split. Its tabs stay where they are, in reading order.
- Chromium's Reverse and its Show stacked or side by side go. Dragging does both.

### Dragging

- A pane's handle is its mini toolbar, Chromium's tag with the favicon, the domain, and a close button. Panes without focus show it, as today. The focused pane shows it while the pointer is on the pane.
- A pane dropped on another pane's edge docks on that side of it. Dropped on its center, the two swap.
- A tab dragged from the sidebar onto a pane's edge joins the split on that side, and moves into the split's task. Dropped on the center, it replaces that pane's tab, which stays open just below the split. A dragged link does the same, as Chromium's drop zones do today.
- A row dragged out of the split's box in the sidebar takes its tab out of the split.
- Dividers drag to resize.
- Esc during a drag puts everything back.

### The sidebar

- A split is one box, as drawn in [Side by side in the sidebar](keyboard-and-mouse.md#side-by-side-in-the-sidebar), with one row per pane.
- Rows are in reading order: by each pane's top-left corner, left to right, then top to bottom. They re-sort when panes move. Dragging a row inside the box does not rearrange the panes.
- The split icon's tooltip reads "Split view".

### Tasks and the organizer

- A split's tabs are all in one task, or all in the Scratchpad.
- The organizer treats a tab in a split as a [kept tab](../CONTEXT.md#language): it does not ask about it or move it. Once the split ends, its tabs are ordinary tabs again.

### Restore

- A restart, and Reopen Closed Split View, bring back the whole layout: its panes, directions, sizes, and the focused pane.

### Pinned split views

- A whole split can be a [pinned split view](../CONTEXT.md#language): one pin icon for the whole layout. Pin on any of its panes, or dragging its box into the pins, pins it. Its tabs leave their task.
- It follows the [pin rules](../product.md#pinned-tabs). It is shared by every window of the profile and moves between windows as one. The window it left shows the whole layout as an [inactive pinned view](../CONTEXT.md#language).
- Adding, closing, or moving a pane, and dragging a divider, change the pin at once. Navigating inside a pane does not change that pane's home.
- Each pane has a home. A web page or uw page has its home URL, a terminal has the folder it was in when pinned, and a chat has the chat itself. "Replace home URL with current" sets each pane's home from where it is now. Reset to home sends every pane home: a busy terminal asks first, then starts a new shell in its folder, and a chat stays as it is.
- Each web pane has its own pinned site. A link that leaves it, and an address a terminal pane prints, open in a Peek preview.
- The agent's tabs from a chat pane open at the top level of the Scratchpad, in the window that shows the pin. They stay the agent's tabs, the organizer leaves them there, and the chat's tab cards list them.
- Cmd+W does nothing in a pinned split, as for any pin. A pane's × closes the pane and takes it out of the pin.
- Unpinning leaves a regular split in the Scratchpad of the window that shows it.
- A single pin cannot join a split, and two pins do not merge.

### Shared tabs

- With the `UwSharedTabs` switch on, a split shows in every window on its workspace with the same layout, per [ADR 0024](../adr/0024-a-workspaces-tabs-show-in-every-window.md). Selecting it in a window moves all its panes' live pages there, and the other window shows them hazed.

### Unchanged

- The agent's Side by side stays a two-pane split of the chat and the agent's tab. It follows the agent by replacing the agent's pane, even after the user adds panes.

## Design

Drawn on 2026-11-04 as T331, on the "Keyboard and mouse" page of the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), with the rules in the "Split views" section of the canvas guide `uw-sideways-drag`. Not signed off yet.

- "Split view: three panes": a page, a terminal, and a page in a nested layout, the mini toolbars, and the sidebar box in reading order.
- "Split view: dragging a pane": a terminal's mini toolbar on its way to the bottom half of another pane.
- "Split view: pinned": the pinned split's tile, its menu, and its hazed layout in another window.
- "Split view: every state": splitting, the order, the 200px limit, closing and ending, the drop zones, drags from and to the sidebar, the divider, the menus and keys, dark, and Edit Homes.

The frames propose, for sign-off:

- Panes inside the page card under one toolbar, as Chromium draws them, with the focused pane's outline in the primary color.
- The drop zone: the half of the pane by the nearest edge, within a quarter of the pane, or the whole pane nearer its middle.
- A pinned split's tile is the same size as every pin, and holds up to four favicons in a shape set by the pane count: two split the tile their way, three are one and two along the first split, and four or more are two by two in reading order, the last cell counting the rest, such as "+6". The user chose both on 2026-11-04, after the first draw made the tile two pins wide and the frame "Split view: the pinned tile at pin size, worst cases (C chosen)" showed that drawing the real layout leaves cells too small for an icon past four panes. Its menu: Reset to Home, Replace Homes with Current, Edit Homes…, Split Right, Split Down, and Unpin Split View. Edit Homes… lists each pane's home; saving loads each changed address in its pane, and a terminal's new folder applies at its next reset.

## How it is built

Per [ADR 0029](../adr/0029-split-views-extend-chromiums-split.md), in a new `uw-split-view.patch`:

- A Chromium split holds any number of tabs. `SplitTabVisualData` carries a layout tree in place of one direction and one ratio: each branch has a direction and its children's sizes, and each leaf is a tab. `TabStripModel`'s split calls, `split_tab_util`, the split menus, and the metrics lose their two-tab checks. The tabs' order in the tab strip is reading order, so Ctrl+Tab and the sidebar follow it.
- `MultiContentsView` lays out a pane per tab and a divider per branch. Its drop target controller gives each pane four edges and a center, and the mini toolbar becomes a drag source.
- The session file saves the tree with the split, beside Chromium's direction and ratio.
- `SplitTabView` (`uw-look.patch`) stacks a row per pane.
- Pinned split views build on uw's live pins (`uw-live-pins.patch`), which move one page between windows today, to move a split's pages and layout together.

## Stages

Each stage is a ticket on the [board](roadmap.md#board). Each one that changes what users see adds to the Help page `site/content/help/split-view.mdx` in the same commit.

1. T331: the design.
2. T332: any number of panes. Splitting, the keys, closing and ending, the sidebar box, the organizer, and restore.
3. T333: dragging.
4. T334: pinned split views.
5. T335: split views with shared tabs on. Needs T253 to T258.

## Acceptance checks

1. Cmd+D in a web page splits the focused pane left and right, and the new right pane shows the command bar, or the new tab page with it off. Cmd+Shift+D splits the focused pane top and bottom. Cmd+D and then Cmd+Shift+D give a left pane beside a top and a bottom pane.
2. Cmd+D in a terminal tab opens a new terminal in the same folder in the new pane. Cmd+D in a chat gives what Cmd+T gives.
3. A split that would make a pane narrower or shorter than 200px does nothing.
4. Bookmark This Tab and Bookmark All Tabs have no key, Cmd+Option+N does nothing, and Keyboard shortcuts lists Split Right and Split Down with their keys.
5. Ctrl+Tab steps through a split's panes in reading order, then leaves the split.
6. Cmd+W closes the focused pane, and the panes beside it take its space. Move Out of Split View leaves the tab open just below the split. Separate Panes ends the split, with its tabs in reading order.
7. The sidebar box has one row per pane, in reading order, and re-sorts after a pane moves.
8. The organizer neither asks about nor moves a tab in a split, and does both again once the split ends.
9. After a restart, and after Reopen Closed Split View, the panes, directions, sizes, and focused pane come back.
10. A pane dragged by its mini toolbar onto another pane's edge docks there, and onto its center swaps the two. A sidebar tab dropped on an edge joins the split, and on a center replaces that tab, which stays open below the split. A row dragged out of the box leaves the split. A divider resizes. Esc during a drag puts everything back.
11. Pin on a pane pins the whole split. Selecting it in a second window moves every pane there and hazes the layout in the first. Adding a pane changes the pin. Reset to home sends every pane home, and a busy terminal pane asks first. The agent's tabs from a chat pane open in the Scratchpad. Cmd+W does nothing. Unpinning leaves a regular split in the Scratchpad.
12. With shared tabs on, a split shows in every window on its workspace with the same layout.
13. Headless screenshots match the frames in light and dark. A [real-window test](../../README.md#real-window-tests) checks the drags, run only when the user asks for it.
14. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

- Whether a split's rows show nesting. Today the box draws none, and a tab in a split is not shown under its parent until the split ends.
