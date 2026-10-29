# Shared tabs

Status: scope agreed with the user on 2026-10-29. The spike, T252, ran the same day; see the [shared tabs spike](../research/shared-tabs-spike.md). The user chose how T253 is built the same day. Nothing else is built.

Implementability: **3/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium gives each window its own tab list, so every window on a workspace holds its own copy of every tab: one live page, and placeholders elsewhere, as [live pins](workspaces.md#how-the-live-pin-page-works) do today. Pins alone scored 4/10. On top of that, the organizer must run once per workspace, a restart must not double tabs, and the first launch merges windows. Up from 2/10 on 2026-10-29, after the spike: a copy costs about what an unloaded tab does, a task stays in step through Chromium's own tab group calls, and Memory Saver's rules work for a page in no window. Extensions, recently closed, tab search, and the command bar still see every copy unless uw hides them.

The user asked on 2026-10-29. Dragging a tab out of a window and then into another window left the middle window open with only its pins, since Chromium counts pins as tabs. The user chose to show a workspace's tabs in every window, as Arc does, before fixing that case on its own. [ADR 0024](../adr/0024-a-workspaces-tabs-show-in-every-window.md) records why.

## Agreed scope

### Sharing

- A workspace has one [tab tree](../CONTEXT.md#language). Every window showing the workspace shows it: the same tasks, Scratchpad, order, nesting, names, colors, and collapsed tasks and branches.
- Each window keeps its own selected tab.
- When one window collapses a task or branch that holds another window's selected tab, it folds there too, but that window's selected tab keeps its row under the folder row until the window selects something else. Its page does not change. Agreed on 2026-10-29.
- Tab search and the command bar list each tab once. Extensions see every window's copy, as unloaded tabs, the way they see a restored tab not loaded yet. None of the [required extensions](../product.md#required-extensions) lists tabs. Agreed on 2026-10-29.
- Each window keeps its hidden window for every other workspace switched to in it, as today, so switching back stays instant. Agreed on 2026-10-29.
- Pins stay shared by every window of the profile, as [Pinned tabs](../product.md#pinned-tabs) says.
- Private windows keep their own tabs and share none.

### One live page

- Each tab has one live page. The window that selected the tab last shows it. Another window with the tab selected shows the hazed picture with "Open here", as a pin does, and a click there brings the live page over.
- Arc keeps a copy of the page in each window, and its own memo says the copies drift apart: a form, a video, or a call ends up in a different state in each. One live page avoids that.

### Windows

- Closing a window closes no tabs. Cmd+Shift+T reopens closed tabs; a closed window leaves nothing to bring back.
- When the last window showing a workspace closes, its pages unload. Each tab keeps its place, title, icon, and history, and loads again when a window shows it.
- A page that is still doing something stays live until it stops. These are the pages Chromium's Memory Saver keeps: one that plays sound, is a [mini player](../CONTEXT.md#language), uses the camera, microphone, or screen, is connected to a device, may send notifications, changed its title or icon in the background (as a timer counting down does), or has unsaved input. [Busy terminals](../CONTEXT.md#language) and tabs the agent is working in stay live too. Pins stay live, as agreed.
- Cmd+N opens a window on the current workspace with a blank tab and the command bar open. The blank tab closes if it is left unused, by picking another tab or closing the window, so blank tabs do not pile up in every sidebar.
- After a restart, each window comes back on its workspace with its selected tab. Each workspace's tree comes back once.

### Dragging

- Dragging a tab out of the sidebar onto the desktop opens a new window on the same workspace with that tab selected. The tab keeps its place in the tree.
- That window closes when the same tab is dragged into another window, unless another tab was selected in it meanwhile. A window used for other tabs stays.
- Dragging a tab into a window on another workspace moves it to that workspace's tree, as Move to Workspace does.

### The last tab

- When a workspace has no tabs left besides pins, every window showing it closes, as Chrome closes a window with no tabs. This holds for Cmd-W, a drag, and Move to Workspace, and for a window that has also shown other workspaces: their tabs stay in their workspaces.
- Close Task and Clear still leave a blank tab instead of emptying a workspace, as agreed on 2026-10-24. Pins do not count toward that check.

### First launch

- The first launch merges each workspace's windows into its tree, front window first. Each window's tasks and Scratchpad tabs join it with their nesting. Two tasks with the same name stay separate. Nothing closes, and each window keeps its selected tab.

## How it works

The user chose copies in each window's tab list on 2026-10-29, after the spike. uw chose the rest for T253 the same day.

- A workspace's tree is a `SharedTree` in `browser/tab_organization`: the page tree, collapsed branches, kept and held tabs, and the names uw gave tasks. Each window's `TabOrganizationController` mirrors its tab strip into it and applies it back, as it did with a tree of its own.
- A window joins its workspace's tree when its first tab arrives, once its workspace is known, and its own tabs go after the workspace's. Private windows, and every window while the switch is off, keep a tree of their own.
- When one window changes the tree, every other window on the workspace follows at once. It opens a copy of each page it lacks, closes its tabs for pages that left, and puts its strip in the tree's order and tasks. Windows do not follow during a drag, since Chromium ends a drag when a tab strip gains a tab; the window that drags brings them up to date when the drag ends.
- Each window keeps its own tab groups and keys each to the tree's task, so one task has a different group id in each window. A rename, a color, or a fold in one window reaches the others.
- A copy is a page with the live page's history and no renderer, as a pin's placeholder is (`page_copy`). Selecting a copy by click or key moves the live page into it with `TabStripModel::DiscardWebContents` and leaves a copy behind. A copy selected any other way, as after the selected tab closed, takes the live page only when no shown window holds it. A navigation that starts in a copy, such as Reload, runs in the live page instead. Copies take the live page's history, title, and icon a moment after it changes them. `InactivePinOverlay` draws the hazed view over a selected copy.
- One organizer acts for each workspace (T255): the organizer of the first window on it. It asks about every page through the page's live page, wherever that is. The other windows' organizers pass it the title changes and the moves by hand they see, and Name Task, Tidy, and an accepted suggestion. They keep track of what their pages show without asking, so when the first window closes, the next one takes over and asks only about what changes after. Undo and the pages counted as opened here belong to the workspace, and every window's sidebar shows what the organizer did.
- A closing window leaves the tree first, so closing it closes no tab in another window. Just before, in the hook Chromium's `UnloadController` gives pins, another window on the workspace takes its live pages, a shown one first. Cmd+Shift+T gets no entry for it (T256).
- The last window on a workspace keeps the workspace's tabs instead of closing: it hides, closes its other workspaces' windows as a closing window does, leaves its group, and is saved as a keeper. Its pages that are doing nothing become copies, and the rest are looked at again each minute. At launch a keeper stays hidden unless no other window would show. When a window shows the workspace again, it takes the keeper's live pages and the keeper closes. Something that shows the keeper itself, such as a click on the Dock, brings it back as an ordinary window (T256).
- When a workspace's last tab besides pins closes, every window on it closes. Close Task and Clear count no pins when they leave a blank tab (T256).
- A tab that moves to another window, by a drag or the tab menu, keeps its page: the window it leaves notes the page and the tree as it was, and the window it reaches takes both. That window's copy of the page makes way. A window opened by moving tabs out joins with them back in their place, and closes once they all move on to another window, unless the user picked another tab in it. Chromium's `TabDragController::EndDrag` ends the drag for the window it began in, wherever it ended (`tab-tree.patch`). That window nests the dragged tabs sideways only while they are in it, since its tab list would drop them from the tree. A window opened during a drag joins once it ends (T257).
- A new window on a workspace with tabs, as from Cmd+N, opens its command bar. Its blank tab closes, while still blank, once the user picks another tab or closes the window (T257).
- Switching workspace still swaps the window, per [ADR 0009](../adr/0009-switching-workspace-swaps-the-window.md). Each hidden partner holds its own workspace's tabs.

Still to build, from the spike:

- A new window that shows first and fills with copies after, the selected tab first: 100 copies take about 0.6 s.
- Some pin and workspace tests expect the old behavior, so T254 updates them for the switch: unpinning in one of two windows, where the unpinned page now becomes a regular tab in both; closing the last window, which now keeps the window hidden, as in `TabOrganizationControllerBrowserTest.TheTreeComesBackInANewWindowAfterTheLastOneCloses`; and closing a window that Cmd+Shift+T brings back, when closing now keeps the tabs.
- A window expands a collapsed branch that hides its own selected tab whenever it reads its strip again, which can undo a fold another window made. T258 keeps the selected row instead, for branches as for tasks.

Rejected on 2026-10-29, not tried: uw draws each workspace's list in the sidebar from a model of its own, and each window's Chromium tab list holds only the pages it has shown. A copy would cost almost nothing, but uw would draw the tree, the tasks, and drags itself again, which [ADR 0002](../adr/0002-tasks-are-native-tab-groups.md) moved away from.

## Design

No new frames are planned. Regular tabs reuse the hazed view from the frame "Pins: the live page in another window" on the [workspaces design](workspaces.md#design), and Cmd+N reuses the command bar. The spike may show a need for frames.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T252: the spike.
2. T253: one tree behind every window on a workspace. Copies come and go, tasks, nesting, collapse, and names stay in step, and one live page moves to the window that selects it, with the hazed view.
3. T255: one organizer per workspace: the automatic organizer, undo, kept and held tabs, Tidy, and Clear, asking only through live pages.
4. T256: windows that close no tabs: unloading unless still running, recently closed, the last tab, and Close Task and Clear leaving out pins.
5. T257: dragging a tab out and back, dragging it to another workspace, and Cmd+N's blank tab.
6. T258: tab search and the command bar listing each tab once, and a folded task keeping another window's selected row.
7. T254: restarts, the first-launch merge, and the switch turned on.

Until T254, shared tabs are behind the Chromium feature `UwSharedTabs`, off by default, so each stage can land without changing the user's browsing. Tests turn it on.

## Acceptance checks

1. Two windows on one workspace show the same tasks, order, nesting, and collapse. A tab opened, moved, nested, renamed, or closed in one changes the other at once. A window on another workspace shows none of it.
2. Selecting a tab in a second window moves its live page there. The first window shows the hazed view, and a click brings the page back. A half-typed form survives both moves.
3. Closing one of two windows on a workspace closes no tab. Closing the last one unloads its pages, except a page playing sound, a page counting down in its title, a page with unsaved input, a busy terminal, and a tab the agent works in. Showing the workspace again loads them.
4. Cmd+N opens a window on the current workspace with the command bar. Esc, then closing the window, leaves no blank tab in the tree.
5. Dragging a tab onto the desktop opens a window on its workspace showing it. Dragging it into the first window closes the new one. If another tab was picked in it first, it stays.
6. Closing a workspace's last tab besides pins closes every window on it. Close Task and Clear on the last tabs leave a blank tab instead.
7. Private windows keep their own tabs.
8. After a restart, each window is back on its workspace with its selected tab, and no tab is doubled.
9. The first launch merges two windows on one workspace into one tree, losing nothing.
10. `pnpm run check` and the focused suites pass. Browser tests stay headless.

## Not in this plan

- Syncing tabs between Macs, which Arc does.
- Arc's single-tab window for a tab dragged out.
- Archiving idle tabs, which Arc does after 12 hours.
