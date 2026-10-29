# A workspace's tabs show in every window

Until 2026-10-29 each uw window kept its own [tab tree](../CONTEXT.md#language), as in Chromium, and only pins were shared. Dragging a tab out and then into another window left the middle window open with only its pins, and closing a window closed tabs with it. On 2026-10-29 we decided that a [workspace](../CONTEXT.md#language) has one tab tree, shown in every window on it, with one live page per tab that moves to the window that selects it, as pins already do. A window is a view of a workspace, so closing one loses no tabs. The [shared tabs plan](../plans/shared-tabs.md) has the details.

Considered options:

- A tree per window, with a window closing once only pins are left. Smaller, and the change first asked for, but closing a window still closes its tabs.
- A tree per profile. Two workspaces on one profile would then differ only in name and theme.
- Arc's copy of each page in each window. Arc's own memo says the copies drift apart: a form, a video, or a call ends up in a different state in each window.

Consequences: Chromium gives each window its own tab list, so each window holds a copy of each tab of its workspace, most of them placeholders. Extensions, Memory Saver, tab search, and open tabs synced to other devices see those copies unless uw hides them. Arc dropped synced tabs in June 2023 because users found them confusing, and brought them back in August 2023 because users missed them.
