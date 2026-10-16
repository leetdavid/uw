# Switching workspace swaps the window

A [workspace](../CONTEXT.md#language) belongs to one profile, and one profile can have several workspaces. A Chromium window belongs to one profile for its whole life, a tab cannot change profile, and Chromium cannot hide some of a window's tabs. So uw cannot switch a window's workspace in place as Arc does. On 2026-10-16 we decided that each window has a partner window for each workspace, whether or not they share a profile. Switching hides the current window and shows its partner at the same place and size, opening the partner on the first switch. Switching back shows the hidden window with its tabs as they were. Several windows can show the same workspace.

Hidden windows keep their pages loaded until Chromium unloads background tabs to save memory.

Considered options: one window that moves every tab out and back in on each switch (rejected: only possible within one profile, and every switch would disturb the tab tree and the organizer); one window per workspace, brought to where the user switched (rejected: it pulls that window away from wherever else it was showing); bringing the other workspace's window to the front where it is, as Chromium's profile menu does (rejected: it does not feel like one window changing).
