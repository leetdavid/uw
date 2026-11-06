# uw's keys say their scope with the second modifier

On 2026-11-07 the user found uw's keys hard to learn: "It's not clear or potentially consistent on how the hotkeys are organized." Every uw key had Ctrl+Cmd except Close Branch and Undo, and Move to New Task needed three modifiers. We decided that the key held with Cmd says what a command acts on, and the letter says what it does:

- Cmd alone: the tab, as in Chromium.
- Option+Cmd: the selected tab and the tabs under it, as Option-click on a tab's × closes them.
- Ctrl+Cmd: its task.

The same letter does the same thing at each scope: Cmd+W closes the tab, Option+Cmd+W the branch, and Ctrl+Cmd+W the task. Two keys break the rule, because Chromium or uw had the key the rule asks for: Ctrl+Cmd with an arrow moves the selected tab and the tabs under it, since Option+Cmd with an arrow switches workspaces and moves focus between panes, and Option+Cmd+Z undoes tab organization, since Ctrl+Cmd+Z goes to the previous task.

On Windows, Cmd becomes Ctrl and Option becomes Alt, so the branch keys are Ctrl+Alt. The Mac's Control has no partner there, so the task keys are Alt+Shift, where Chromium already puts its tab group keys on Windows.

Holding a scope's two keys for half a second shows its [key card](../CONTEXT.md#language), so the rule can be learned from the keys themselves.

Considered options: keeping Ctrl+Cmd for every uw command and Option only for a Chromium key with more reach (rejected: Move to New Task still needed a third modifier or a new letter, and the keys stayed a list to memorize); Ctrl+Shift for the task keys on Windows (rejected: Chromium has N, W, T, and R there).

Consequences: New Child Tab, Move to New Task, Move to Scratchpad, Keep Here, and Collapse Branch moved to Option+Cmd, and their old keys stopped working. Keys the user changed in Keyboard shortcuts stay theirs. A new uw command picks its modifier by what it acts on.
