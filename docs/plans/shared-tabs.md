# Shared tabs

Status: scope agreed with the user on 2026-10-29. The spike, T252, ran the same day; see the [shared tabs spike](../research/shared-tabs-spike.md). How T253 is built waits for the user's choice. Nothing else is built.

Implementability: **3/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium gives each window its own tab list, so every window on a workspace holds its own copy of every tab: one live page, and placeholders elsewhere, as [live pins](workspaces.md#how-the-live-pin-page-works) do today. Pins alone scored 4/10. On top of that, the organizer must run once per workspace, a restart must not double tabs, and the first launch merges windows. Up from 2/10 on 2026-10-29, after the spike: a copy costs about what an unloaded tab does, a task stays in step through Chromium's own tab group calls, and Memory Saver's rules work for a page in no window. Extensions, recently closed, tab search, and the command bar still see every copy unless uw hides them.

The user asked on 2026-10-29. Dragging a tab out of a window and then into another window left the middle window open with only its pins, since Chromium counts pins as tabs. The user chose to show a workspace's tabs in every window, as Arc does, before fixing that case on its own. [ADR 0024](../adr/0024-a-workspaces-tabs-show-in-every-window.md) records why.

## Agreed scope

### Sharing

- A workspace has one [tab tree](../CONTEXT.md#language). Every window showing the workspace shows it: the same tasks, Scratchpad, order, nesting, names, colors, and collapsed tasks and branches.
- Each window keeps its own selected tab.
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

## How it could work

Proposed by uw on 2026-10-29 and tried in the spike. The user has not chosen yet.

- Each window's Chromium tab list holds every tab of its workspace. One window holds a tab's live page and the others hold placeholders, as `PinSync` does for pins with `TabStripModel::DiscardWebContents`.
- Switching workspace still swaps the window, per [ADR 0009](../adr/0009-switching-workspace-swaps-the-window.md). Each hidden partner holds its own workspace's tabs.
- One page tree and one organizer per workspace, copied into each window's tab groups and indents. A copy moves a task as a whole before it places single tabs, or the task splits.
- A new window shows first and fills with copies after, the selected tab first: 100 copies take about 0.6 s.
- Unloading asks Chromium's `DiscardEligibilityPolicy` which pages it would keep, as Memory Saver does, and adds uw's own cases. The spike found it answers for a page in no window, and never unloads a copy.
- uw leaves a closed window out of recently closed.

The other way, not tried: uw draws each workspace's list in the sidebar from a model of its own, and each window's Chromium tab list holds only the pages it has shown. A copy would cost almost nothing, but uw would draw the tree, the tasks, and drags itself again, which [ADR 0002](../adr/0002-tasks-are-native-tab-groups.md) moved away from.

Open questions for T253:

- Whether extensions, tab search, and the command bar see copies, or uw hides them.
- What a window shows when another window collapses the task that holds its selected tab. Chromium keeps the selection.
- Whether a hidden partner closes after a while to save its copies, since closing it loses no tabs.

## Design

No new frames are planned. Regular tabs reuse the hazed view from the frame "Pins: the live page in another window" on the [workspaces design](workspaces.md#design), and Cmd+N reuses the command bar. The spike may show a need for frames.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T252: the spike.
2. T253: one tab tree per workspace: sharing, the live page, windows, dragging, and the last tab.
3. T254: restarts and the first-launch merge.

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
