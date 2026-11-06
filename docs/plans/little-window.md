# Little windows

Status: scope agreed on 2026-11-07 in a grilling session, after the user asked for "a cmd+opt+n (ctrl+alt+n for windows) to open a mini window like arc does that is kind of meant to be temporary". Arc calls it Little Arc. The user pre-approved the design and asked for every stage to be built, then released for macOS and Windows. T357 to T362 are on the [board](roadmap.md#board).

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium has the pop-up window, a listener for a key pressed in any app on the Mac and Windows, moving a live page between windows, and the session rules to leave a window out of restore. The risks: drawing uw's page card, theme, and an editable toolbar in a pop-up, which keeps Chromium's layout today; showing the window on the Space the user is on, over a full-screen app too; and checking all of it twice, for Windows.

## Agreed scope

A [little window](../CONTEXT.md#language) is a small, temporary window for a quick look at one page. Underneath it is a Chromium pop-up window that uw marks as its own. See [ADR 0033](../adr/0033-a-little-window-is-a-chromium-pop-up-that-looks-like-a-regular-window.md).

### Opening one

- Option+Cmd+N opens a little window, from uw and from any other app while uw runs. Ctrl+Alt+N on Windows.
- File → New Little Window and the new button's ▾ menu open one too, with the key beside them.
- Option+Cmd-click on a link, Ctrl+Alt-click on Windows, opens the link in a little window. It takes that click from Chromium's "open in split view". A link's right-click menu has "Open Link in Little Window" with Chromium's other "Open Link in…" items.
- A tab in the sidebar does not open in a little window.
- A little window opened by the key or a menu opens blank, with the [command bar](command-bar.md) open in it. What the user picks there loads in the little window. Esc before anything is picked closes the window.
- In a little window the command bar finds the web, addresses, history, and bookmarks. Picking an open tab goes to that tab and closes an empty little window. Commands that act on tabs do not show.
- Each press opens another little window.

### Its workspace

- A little window belongs to the [workspace](../CONTEXT.md#language) of the uw window used last: that profile's logins and cookies, its history, and its theme.
- It is never private, even when a private window was used last.

### What it looks like

- A regular uw window without the sidebar: the page card, the theme around it, and the full toolbar with back, forward, reload, the address, extensions, and the ⋮ menu. The window buttons sit at the toolbar's left.
- A click on the address, or Cmd+L, opens the command bar to change it, as in any window ([ADR 0005](../adr/0005-the-command-bar-replaces-typing-in-the-address-bar.md)).
- Move to Workspace sits at the toolbar's right, with a ▾ beside it for the [workspace menu](#keeping-the-page).
- The first little window opens in the middle of the screen the user is working on, about 900 by 650. Later ones open at the size the user last left one, each a little offset from the one before.
- It opens on the Space the user is looking at, and over a full-screen app too. It never moves the user to another Space.
- It is an ordinary window. Other apps' windows can cover it.

### Keeping the page

- Move to Workspace, Cmd+O, moves the live page, with its scroll, form text, sound, and back and forward list, to the top of the [Scratchpad](../product.md#scratchpad), selected, in the window used last on the little window's workspace. That window comes to the front and the little window closes. The organizer then files the tab by its usual rules, as for a [link from another app](../product.md#automatic-tab-organization).
- When no window shows the workspace, a new window opens on it.
- Cmd+Shift+O, or the ▾, opens the workspace menu. Picking a workspace on the same profile moves the live page there. Picking one on another profile opens the address there and closes the little window, as [Move to workspace](workspaces.md#workspaces) does for a tab.
- Cmd+O and Cmd+Shift+O mean this only in a little window. Elsewhere Cmd+O stays Open File…, and Ctrl+Shift+O stays the Bookmark Manager on Windows.

### Links inside it

- A link that opens a new tab, such as a Cmd-click, opens at the top of the Scratchpad in the window used last on the little window's workspace. A Cmd-click opens it in the background and the little window stays in front. A link that opens in front brings that window forward.
- A site's own pop-up, such as a sign-in window, stays a Chromium pop-up.

### Closing

- Cmd+W closes a little window without asking, unless its page asks first.
- A little window closes on its own after it has not been the front window for a set time: 1 hour, 6 hours (the default), 12 hours, 1 day, or Never. It stays open while its page plays sound, and while its page would ask before it is left.
- Cmd+Shift+T and History → Recently Closed bring a closed little window back, as a little window.
- Little windows do not come back after uw quits.

### What sees it

- History keeps its pages, in its profile.
- macOS's Window menu lists it, and Cmd+\` reaches it.
- The organizer, the command bar's open tabs, the agent's [tab tools](tab-tools.md), and the [uw command](uw-command.md) leave it out.

### Links from other apps

- A switch, off by default, opens [links from other apps](../product.md#automatic-tab-organization) in a little window instead of at the top of the Scratchpad.

### Settings

A Little windows section in uw's settings holds:

- Option+Cmd+N in uw: on.
- Option+Cmd+N from other apps: on.
- Open links from other apps in a little window: off.
- Close a little window after it is unused for: 6 hours.

With both key switches off, the key does nothing. The key itself is changed in [Keyboard shortcuts](keyboard-and-mouse.md#keyboard-shortcuts), and the switches name whatever key it has. Keyboard shortcuts lists Open File… as "Open File…, or Move to Workspace in a little window", so Cmd+O stays one row.

### Keys

- Option+Cmd+N was Move to New Task's under [Keys by scope](keyboard-and-mouse.md#keys-by-scope). Move to New Task now has no key of its own. Once [Move to Task…](keyboard-and-mouse.md#move-to-task) is built, Option+Cmd+M, then Return, does it, since that list starts with it. Until then it is in the menus only.
- The little window's key goes to macOS before any app sees it, so the app in front never gets Option+Cmd+N while the switch is on. Finder's New Smart Folder and Mail's New Viewer Window lose it. Arc took the same key.

### Windows

- Ctrl+Alt+N, with the same switches.
- On Windows, AltGr sends Ctrl+Alt. When the keyboard types a letter with AltGr+N, such as "ń" on Polish keyboards, uw does not take Ctrl+Alt+N from other apps. The settings section says why and links to Keyboard shortcuts. Inside uw the key still works.

## Not in scope

- Opening a sidebar tab in a little window.
- Little windows that stay on top of other apps. The [mini player](mini-player.md) does that for videos.
- Peek previews from pinned tabs. They stay a card over the page, as in Arc; see [Pinned tabs](../product.md#pinned-tabs).
- Private little windows.

## Design

Drawn on 2026-11-07 as T357, on the "Little windows" page of the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), with the rules in the "Little windows" section of the canvas guide `uw-hidden-sidebar`. The user pre-approved the design.

- "Little window: the command bar": Option+Cmd+N pressed in Mail, the little window over it with the command bar.
- "Little window: Move to Workspace": a page in a little window, with the workspace menu open.
- "Little windows: every state": the menus that open one, a link's menu, light, dark, and behind another window, the command bar before typing, a Cmd-click, the page after Move to Workspace, over a full-screen app, the settings, Keyboard shortcuts, and Windows with the AltGr note.

## Stages

Each stage is a ticket on the [board](roadmap.md#board), built and checked on the Mac and on Windows.

1. T357: the design.
2. T358: the little window in uw: the key in uw, the menus, the command bar, the look, Move to Workspace and its menu, links inside it, closing, restore, what sees it, and Move to New Task's key.
3. T359: the key from other apps: its switch, the Space and full screen, and the AltGr rule on Windows.
4. T360: Option+Cmd-click and Open Link in Little Window.
5. T361: links from other apps, behind their switch.
6. T362: closing unused little windows, and its setting.

## Acceptance checks

1. With uw in front, Option+Cmd+N opens a little window with the command bar. An address picked there loads in it. Esc before a pick closes the window.
2. File → New Little Window and the ▾ menu's New Little Window do the same and show the key.
3. The little window has no sidebar, takes the theme of the workspace used last, and has the full toolbar and Move to Workspace. A click on the address opens the command bar. A window screenshot matches the frames.
4. Cmd+O moves the page, the same live page with its form text, to the top of the Scratchpad in the workspace's window used last, selected, and the little window closes. With no window on that workspace, one opens. Cmd+Shift+O lists the workspaces, and picking another one on the same profile moves the page there.
5. A Cmd-click in a little window opens a background tab at the top of the Scratchpad, and the little window stays in front.
6. Option+Cmd-click on a link and "Open Link in Little Window" open the link in a new little window.
7. The organizer, the command bar's open tabs, the agent's tab tools, and the `uw` command leave the little window out. History has its page.
8. Cmd+W closes it, Cmd+Shift+T brings it back as a little window, and after a restart it is not there.
9. With uw in the background and no window open, Option+Cmd+N opens a little window in front, on the Space in view. With that switch off, it does not. With the in-uw switch off, the key does nothing in uw.
10. With the links switch on, `open <url>` opens a little window. With it off, the link opens at the top of the Scratchpad.
11. A little window unused for the set time closes, unless its page plays sound or would ask before it is left. Never keeps it.
12. Option+Cmd+N no longer moves a tab to a new task.
13. In a Windows test build, checks 1 to 12 pass with Ctrl and Alt. With a Polish keyboard, Ctrl+Alt+N is not taken from other apps, and settings says why.

Browser tests stay headless. A key pressed in another app, the Space, and full screen are `MANUAL_` tests run with `pnpm run window-tests`.

## Open questions

- AltGr also sends ADR 0032's Ctrl+Alt branch keys on Windows, inside uw's windows. Whether those need the same rule is for the [Windows plan](windows.md).
