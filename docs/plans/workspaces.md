# Workspaces and shared pins

Status: scope agreed with the user between 2026-10-16 and 2026-10-21. Not built. The designs, T128, come first.

Implementability: **4/10** for the whole plan, using the [roadmap convention](roadmap.md#implementability-estimates). Each stage has its own score on the [board](roadmap.md#board). Shared pins and the window swap reuse Chromium's profiles and windows. The swipe that follows the fingers and the live pin page are the risky parts, so they come last.

## Agreed scope

### Workspaces

- A [workspace](../CONTEXT.md#language) is Arc's space: a name, an emoji icon, and a theme. Each belongs to one [profile](../CONTEXT.md#language), and a profile can have several. Workspaces separate areas such as work and personal. They do not replace tasks, which organize the tabs inside a workspace.
- Every workspace shows in the switcher at the foot of the sidebar, in every window, from launch. A workspace can be deleted but not hidden.
- Switch by clicking a workspace's icon, with Ctrl+1 to 9, with Cmd+Option+← / →, or with a two-finger swipe on the sidebar. The slide follows the fingers, and a half swipe snaps back. Cmd+Option+← / → stop switching tabs; Ctrl+Tab and Cmd+Shift+[ / ] still do. A swipe on the page stays back and forward.
- A window shows one workspace at a time. Switching replaces the window with its partner for the other workspace, at the same place and size. Switching back shows the tabs as they were. Several windows can show the same workspace. See [ADR 0009](../adr/0009-switching-workspace-swaps-the-window.md).
- Cmd+N opens a window in the current window's workspace.
- Closing a window closes all of its workspaces. Cmd+Shift+T brings them back together.
- After a quit, each window comes back showing the workspace it showed. Its other workspaces come back behind it with their tabs, which load only when shown.
- "+" after the workspace icons and File → New Workspace open a form with a name, an emoji icon, and a profile. The profile defaults to the current workspace's. "New profile…" asks only for a name, with no Google sign-in. The window then switches to the new workspace and the theme picker opens.
- Right-clicking a workspace's icon offers Rename, Change icon, Theme…, and Delete. Delete asks first, then removes the workspace and its tabs. When it is the profile's last workspace, uw also asks "Also delete the profile 'Work' and its logins and history?", with Keep as the default.
- Each existing profile becomes a workspace, with the profile's name and theme.
- Chromium's profile button and menu are removed, and its profile picker never shows. Chromium's settings pages stay.
- Each workspace has its own [theme](../CONTEXT.md#language): everything the theme picker sets.
- The organizer works inside one workspace and never moves a tab to another. Right-click → Move to workspace ▸ moves a tab by hand. To a workspace on the same profile, the live page moves. To one on another profile, the address opens there and the tab closes here.
- The command bar finds open tabs in every workspace of the current profile, and picking one switches to its workspace. It never lists another profile's tabs. Each workspace is a "Go to …" command, and "New workspace" is one too.
- Private windows have no workspaces and no switcher.
- [Providers](../CONTEXT.md#language) are shared by every profile. Each profile picks its own language, decision, and agent models. Chats, organizer settings, and what the command bar learns stay per profile.

### Shared pins

- The [pinned-tab behavior](../product.md#pinned-tabs) agreed in Product. A pin belongs to a profile and shows in every window and workspace of it.
- Until T133, each window loads its own page for a pin.

## Design

T128 draws, on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), a new page "Workspaces":

- The sidebar foot with the workspace icons beside Organize and the new button.
- The new workspace form, with the profile list.
- A workspace icon's menu, and the delete question for a profile's last workspace.
- Move to workspace ▸ in a tab's menu.
- A pin's menu with Reset to home, Replace home URL with current, and Edit home URL….

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
10. Two workspaces on one profile show two themes.
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
