# Workspaces and shared pins

Status: scope agreed with the user between 2026-10-16 and 2026-10-21. The designs, T128, were signed off on 2026-10-21. T129, pins shared by a profile's windows, is built and in review. T130, workspaces themselves, was built on 2026-10-21 and is in review. T133, the live pin page, was built on 2026-10-22 and is in review. T132, the swipe, was built on 2026-10-22 and is in review.

Implementability: **4/10** for the whole plan, using the [roadmap convention](roadmap.md#implementability-estimates). Each stage has its own score on the [board](roadmap.md#board). Shared pins and the window swap reuse Chromium's profiles and windows. The swipe that follows the fingers and the live pin page are the risky parts, so they come last.

## Agreed scope

### Workspaces

- A [workspace](../CONTEXT.md#language) is Arc's space: a name, an emoji icon, and a theme. Each belongs to one [profile](../CONTEXT.md#language), and a profile can have several. Workspaces separate areas such as work and personal. They do not replace tasks, which organize the tabs inside a workspace.
- Every workspace shows in the switcher at the foot of the sidebar, in every window, from launch. A workspace can be deleted but not hidden.
- Switch by clicking a workspace's icon, with Ctrl+1 to 9, with Cmd+Option+← / →, or with a two-finger swipe on the sidebar. As in Arc, only the sidebar's contents slide, following the fingers, while the theme blends; a half swipe snaps back. The page stays put. Once the swipe lands it crossfades to the other workspace's page, with a 12px nudge in the swipe's direction. Cmd+Option+← / → stop switching tabs; Ctrl+Tab and Cmd+Shift+[ / ] still do. A swipe on the page stays back and forward.
- Agreed on 2026-10-22 for the swipe: a workspace whose window has not been shown since launch slides in as its tint alone, and its tabs show as it lands. A hidden sidebar has no swipe, and there keys and clicks only fade the page. With Reduce Motion, a swipe still follows the fingers, and keys and clicks only fade the page. The sidebar's swipe works whatever "Swipe between pages" is set to.
- A window shows one workspace at a time. Switching replaces the window with its partner for the other workspace, at the same place and size. Switching back shows the tabs as they were. Several windows can show the same workspace. See [ADR 0009](../adr/0009-switching-workspace-swaps-the-window.md).
- Cmd+N opens a window in the current window's workspace.
- Closing a window closes all of its workspaces. Cmd+Shift+T brings them back together.
- After a quit, each window comes back showing the workspace it showed. Its other workspaces come back behind it with their tabs, which load only when shown.
- New Workspace, in the new button's menu and in File, opens a form with a name, an emoji icon, and a profile. The profile defaults to the current workspace's. "New profile…" asks only for a name, with no Google sign-in. The window then switches to the new workspace and the theme picker opens.
- Agreed on 2026-10-24: nothing names the workspace under the pins, since the foot's icon already says which one the window shows.
- Right-clicking a workspace's icon offers Rename, Change icon, Theme…, and Delete. Rename opens a card above the sidebar's foot with the emoji and the name in a field: Return or a click elsewhere saves, and Esc leaves the name as it was. Delete asks first, then removes the workspace and its tabs. When it is the profile's last workspace, uw also asks "Also delete the profile 'Work' and its logins and history?", with Keep as the default.
- Each existing profile becomes a workspace, with the profile's name and theme.
- Chromium's profile button and menu are removed, and its profile picker never shows. Chromium's settings pages stay.
- Each workspace has its own [theme](../CONTEXT.md#language): everything the theme picker sets. That includes Light, Dark, and Auto, and web pages in the workspace's windows follow it, so a dark workspace shows sites in their dark style. Agreed on 2026-10-24.
- The organizer works inside one workspace and never moves a tab to another. Right-click → Move to workspace ▸ moves a tab by hand. To a workspace on the same profile, the live page moves. To one on another profile, the address opens there and the tab closes here.
- The command bar finds open tabs in every workspace of the current profile, and picking one switches to its workspace. It never lists another profile's tabs. Each workspace is a "Go to …" command, and "New workspace" is one too.
- Private windows have no workspaces and no switcher.
- [Providers](../CONTEXT.md#language) are shared by every profile. Each profile picks its own language, decision, and agent models. Chats, organizer settings, and what the command bar learns stay per profile.

### Shared pins

- The [pinned-tab behavior](../product.md#pinned-tabs) agreed in Product. A pin belongs to a profile and shows in every window and workspace of it.
- Until T133, each window loads its own page for a pin. With T133, one live page per pin moves between windows, as [Pinned tabs](../product.md#pinned-tabs) says.

## How pins are kept

Chosen by uw for T129 on 2026-10-21.

- A profile's pins, each an id and a home URL, are the profile pref `uw.pins`. `PinStore` in `browser/pins` holds them.
- Each normal window has a `PinSync`. It marks every pinned tab with its pin's id, and the mark stays with the page if the tab moves to another window. Pinning, unpinning, or moving a pin in a window changes the store. A change to the store reaches every window on a later task.
- A window adds a missing pin as a background tab at its home URL when no window has the pin's live page; see [How the live pin page works](#how-the-live-pin-page-works). It closes a copy of a pin that was unpinned elsewhere, keeps one copy of each pin (a restored one first), and adopts pinned tabs the store does not know, such as tabs pinned before uw kept pins. A copy counts as restored only until its window first applies the store, so a pin unpinned later never comes back as a new one.
- Each pinned tab saves its pin's id in Chromium's session file under `uw.pin`, so a restart brings it back on the page it was on, with no second copy.
- Cmd-W, a middle-click on the tile, and the tab menu never close a pin. The menu's three rows and the Edit Home URL card are in `browser/ui/views/pins`.
- Built differently from the design: Reset to Home does not show "Double-click" at its end, since Chromium's menus show only keys there. The card uses Chromium's dialog title and buttons in the shape of a uw card.

## How the live pin page works

Chosen by uw for T133 on 2026-10-22, and built the same day.

- Each pin has one live page in its profile. Every other window's tab for the pin holds a placeholder: a page with the live page's history and no renderer, as Chromium's discarded tabs have. Its tile shows the live page's title and icon, refreshed a moment after the live page changes them or navigates.
- Selecting a placeholder moves the live page into that tab and leaves a placeholder where it was. The pages swap inside the two windows' own tabs with `TabStripModel::DiscardWebContents`, the call Chromium's tab discarding uses, so no sidebar gains or loses a tile. Two quick selections move it twice and leave one live page.
- Over a selected placeholder, `InactivePinOverlay` in `browser/ui/views/pins` draws the design's hazed view: a picture of the page taken as it last left a window on screen, blurred and dimmed, with "Open here" in the middle. A click anywhere on it brings the live page.
- A navigation that starts in a placeholder, such as Reload or Back, stops there, brings the live page over, and runs in it, so no second copy loads.
- Showing a hidden workspace window brings in the live page of its selected pin when the page sits in the window that just went behind it, or in no window. `WorkspaceWindow::ObserveReveals` says when.
- Before a closing window asks its pages whether it may close, its live pin pages move to `PinPages` in `browser/pins`. It keeps them, and their profile, alive until a window takes one, the profile is deleted, or uw quits. A new window takes them at once, and so does a window Cmd+Shift+T brings back. When uw quits, the session keeps the page in its window instead, so a page that asks before it is left still asks then.
- Each placeholder saves `uw.pin_placeholder` in the session file beside `uw.pin`. A restart, or Cmd+Shift+T, brings it back as a placeholder before it loads, and so does a copy whose live page another window already holds, such as the second copy in a session saved before T133. Cmd+Shift+T keeps both keys, so a window it brings back has its pins, not new ones.
- Unpinning a placeholder closes it, and the live page stays a regular tab where it is, or opens in its place when no window has it.
- The `HandleBeforeClose` hook and the profile's new keep-alive reason are in the `uw-live-pins` patch. Restoring placeholders is in `tab-tree`, and keeping the keys through Cmd+Shift+T in `uw-workspaces`.
- Built differently from the design: the picture is of the page, so a light page stays light under the dark haze. In dark mode the pill takes the dark card's field color, `#171717` on uw's black.

## How workspace windows work

Chosen by uw for T130 on 2026-10-21.

- The workspaces are the local state pref `uw.workspaces`: each one's id, name, emoji, and profile folder, in the switcher's order. `WorkspaceList` in `browser/workspaces` holds them. At first use, each profile becomes a workspace with its name and an unused emoji.
- Each normal window that is not private has a `WorkspaceWindow`. It knows the workspace the window shows and the group of partner windows it belongs to. A new window takes the workspace of the window in front when they share a profile, and otherwise its profile's first workspace.
- Switching moves the group's window for the other workspace to where the current one is and orders it in just under it, where it draws unseen. Once its page and then the window have drawn, or after 200 ms, it comes to the front and the current one hides. Neither window plays macOS's open or close animation then, so the window never seems to shrink or reload; a new window from Cmd+N still animates. The first switch to a workspace opens its window with a new tab, once the profile has loaded.
- A hidden window that something else shows becomes the one its group shows: a link from another app, an extension that shows it without focus, or macOS ordering it in some other way. macOS already leaves hidden windows out of its Window menu, and Bring All to Front leaves them hidden.
- Each window saves its workspace, its group, and whether it is hidden, in Chromium's session file and in Cmd+Shift+T's record of a closed window. A restart brings the group back with the same window in front. Cmd+Shift+T on any window of a closed group brings back the rest. If a group's shown window does not come back, one of its hidden windows shows. A hidden window's tabs load only once it shows: the one in front when it shows, the others when picked. Until then each shows its saved icon, as Chromium's restored tabs that load later do, and a pin's placeholders take it from the pin's page. Its pages are hidden while it is, since macOS counts a page in a window that is only ordered out as visible.
- Closing the shown window closes its group, except when quitting. When the group's windows were the profile's last, the session keeps all of them for the next launch.
- The swipe, from T132, is `WorkspaceSwipe` in `browser/ui/views/workspaces`. It sees the sidebar's scrolls before the tabs do. Once the fingers have moved 15px more across than up or down, it keeps the scroll, and the momentum macOS sends after it, from the tabs. A picture of the tab strip, taken then, slides with the fingers on a layer over the sidebar, beside the other workspace's tab strip as its window looked when it last slid away, or only its tint without one. The layer reaches down to the sidebar's foot, not just to the end of this workspace's tabs, so a workspace with more tabs slides in with all of them. Between two workspaces of one profile the slide starts under the pins, which stay put. The tint is drawn twice while it blends, and the switcher's fill glides between the icons. Past half the tab strip's width, or on a flick of 0.3px per millisecond from 30px on, the other workspace lands. Once the fingers pass halfway, the other workspace's page starts to load, opening its window hidden if there is none yet, and it keeps loading if the swipe goes back. The window takes a picture of its page and switches, and the window that shows lays that picture over its own page and fades it out in 150 ms, the two 12px apart. Ctrl+1 to 9, Cmd+Option+← / →, and a click on a workspace play the same slide in 175 ms.
- Ctrl+1 to 9 and Cmd+Option+← / → are window commands beside the task commands. New Workspace is off in windows without a sidebar, such as pop-ups. While the sidebar is hidden, New Workspace opens its card at the window's bottom-left corner, and the keys still work.
- The command bar's open-tab rows already come from every window of the current profile, hidden ones included, and picking one shows its window. Its "Go to …" and "New workspace" rows come with T61, which adds command rows.
- The switcher, the workspace cards, and Move to Workspace are in `browser/ui/views/workspaces`. Chromium's sidebar puts the switcher in the middle of the Organize row. Organize's tree icon, and the smile, palette, person, and up-and-down icons, are uw's own, in `browser/ui/vector_icons`.
- Deleting a workspace switches each window that shows it to the workspace before it, then closes its windows. Cmd+Shift+T forgets them and any of the workspace's windows that closed before. A window that still comes back with a deleted workspace, as when its profile was not loaded at the time, shows on its own in its profile's first workspace. With Delete Profile Too, the profile goes once those windows have closed. One workspace always stays.
- Every profile shares the providers, through local state, as the [AI providers plan](ai-providers.md) says.
- Each workspace keeps its theme, Light, Dark, or Auto included, in `uw.workspaces`. Built for T131 on 2026-10-24. A workspace without one takes its profile's when its first window starts, and a new workspace starts on the first suggested theme no workspace uses, in the mode of the window it was made from. `BrowserWidget::GetColorProviderKey` asks `ApplyThemeOfWindow` (`browser/workspaces/workspace_theme.h`) for the window's color and mode, so the window and the pages in it, which take their mode from it, show the workspace's theme. The tint and the grain come from the same theme, and the swipe blends toward the other workspace's. The theme picker changes the theme of the window it opened in. A change made outside the picker, such as a color picked in the "Customize Chromium" panel or the mode in Chromium's settings, goes to the workspace of the profile's front window.
- Chromium's profile button, its row in the app menu, Cmd+Shift+M, the Profiles menu in the menu bar, and the profiles in the Dock menu are gone. Launching never shows the profile picker, unless a policy forces it.
- Built differently from the design: a workspace's tooltip writes its key as the menus do, "⌃2", not "Ctrl+2". The new button's menu and the profile list are Chromium's menus, so their rows have no icons, and a profile's emoji sit where a key would. The new workspace and delete cards use Chromium's dialog title and buttons in the shape of a uw card. The new profile step's button reads "Done", and Delete Profile Too sits at the card's left. Change Icon, which the design does not draw, opens a card with the emoji, the name, and the suggestions.

## Design

On the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Workspaces", drawn on 2026-10-21 for T128 and signed off the same day:

- "Workspaces: the sidebar": one row at the sidebar's foot, as in Arc, with Organize as an icon at the left, the workspaces centered, and the new button at the right. New Workspace moves into the new button's menu. The workspace's name sat under the pins until 2026-10-24. The user picked this row over two rows on 2026-10-21, "for now"; both are in "Sidebar foot: two balanced layouts".
- "Workspaces: the swipe": both windows sliding with the fingers.
- "New workspace: the card": name, emoji, and profile.
- "Workspaces: every state": the foot with five or more workspaces and in dark mode, the new button's menu, the Organize icon, the workspace menu, renaming, the profile list, a new profile, both delete questions, and Move to Workspace.
- "Pins: every state": the pin menu, Edit Home URL, one set of pins in two windows, and in two workspaces of one profile.
- "Pins: the live page in another window", drawn for T133 and approved on 2026-10-22: the hazed picture with "Open here" in light and dark mode, and notes on a click, a workspace switch, unpinning, and closing a window.
- "Rename workspace: the card" and "Rename workspace: every state", drawn and approved on 2026-10-24: no name under the pins, and the rename card above the foot, with a long name and in dark mode.

The rules are in the `uw-workspaces` guide. The dots from five workspaces on, and starting a new workspace on an unused suggested theme are uw's proposals.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T128: the designs on the canvas, signed off by the user.
2. T129: shared pins. The same pins and home URLs in every window of a profile, Cmd-W does nothing on a pin, double-click and Reset to home, and the two home URL items.
3. T130: workspaces. The switcher, the keys, the window swap, creating, renaming, and deleting, the profile button removed, closing and restoring, the command bar, Move to workspace, and providers shared by every profile. Switching is instant until T132.
4. T131: a theme per workspace.
5. T132: the swipe that follows the fingers.
6. T133: the live pin page that moves between windows. A prototype first, then the build.

## Acceptance checks

1. A pin added in one window shows in every window of its profile, in the same order. Unpinning removes it everywhere. Another profile's windows never show it.
2. Cmd-W on an active pin does nothing. Double-clicking a pin, or Reset to home, loads its home URL. Replace home URL with current saves the address without loading. Edit home URL… saves and loads the new one.
3. With three workspaces, every window shows three icons at launch. Clicking an icon, Ctrl+2, and Cmd+Option+→ each switch.
4. Switching replaces the window at the same place and size. Switching back shows the same tabs, with a half-typed form still filled in.
5. Two workspaces on one profile share a site's sign-in. Two on different profiles do not.
6. Closing a window closes its other workspaces. Cmd+Shift+T brings all of them back.
7. After a quit and launch, each window shows its last workspace, and switching shows the others with their tabs.
8. Deleting a profile's last workspace asks about the profile, and Keep leaves its logins and history.
9. The toolbar has no profile button, and launching never shows the profile picker.
10. Two workspaces on one profile show two themes. With one set to Dark and the other to Light, a page in each sees `prefers-color-scheme` as its workspace says.
11. The organizer never files a tab into another workspace. Move to workspace keeps a page's state within a profile and reopens the address across profiles.
12. The command bar lists a tab from another workspace of the same profile and switches there. It never lists another profile's tabs.
13. A half swipe on the sidebar snaps back, and a full one follows the fingers to the next workspace.
14. A live pin page moves between windows as [Product](../product.md#pinned-tabs) describes.
15. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Not in this plan

- Arc's pinned tabs that belong to one space. uw has only pins shared by a profile, for now.
- Peek previews from pins. They stay a separate row in the [roadmap](roadmap.md#current-features).
- Showing a workspace's tabs in every window, as Arc does. Each window keeps its own [tab tree](../CONTEXT.md#language).
- Syncing workspaces between Macs.
