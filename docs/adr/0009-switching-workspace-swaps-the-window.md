# Switching workspace swaps the window

A [workspace](../CONTEXT.md#language) is a profile, and a Chromium window belongs to one profile for its whole life: a tab cannot change profile, and tabs only move between windows of the same profile. So uw cannot switch a window's workspace in place as Arc does. On 2026-10-16 we decided that each window has a partner window for each workspace. Switching hides the current window and shows its partner at the same place and size, opening the partner on the first switch. Switching back shows the hidden window with its tabs as they were. Several windows can show the same workspace.

Hidden windows keep their pages loaded until Chromium unloads background tabs to save memory.

Considered options: several workspaces sharing one profile, as Arc allows (rejected: Chromium cannot hide tabs, so every switch would move every tab out of the window and back); one window per workspace, brought to where the user switched (rejected: it pulls that window away from wherever else it was showing); bringing the other workspace's window to the front where it is, as Chromium's profile menu does (rejected: it does not feel like one window changing).
