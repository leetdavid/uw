# A little window is a Chromium pop-up that looks like a regular window

On 2026-11-07 the user asked for Arc's Little Arc: Option+Cmd+N, from any app, opens a small temporary window for a quick look at a page. We decided that a [little window](../CONTEXT.md#language) is a Chromium pop-up window that uw marks as its own, drawn like a regular uw window with its sidebar gone: the page card, the theme around it, and the full toolbar, whose address opens the command bar as in any window. A pop-up is already left out of workspaces, pins, shared tabs, the organizer, and the agent's tab tools, holds one page, and sends links that open a new tab to an ordinary window, which is what a little window needs.

Considered options:

- An ordinary window with its sidebar hidden and no workspace. It already looks right, but since [ADR 0024](0024-a-workspaces-tabs-show-in-every-window.md) every ordinary window shows a workspace's tabs, so shared tabs, pins, the workspace swap, and every place uw looks for ordinary windows would need an exception. It would also still take new tabs.
- A window of uw's own with only a page in it. uw would rebuild what Chromium's windows already do: back and forward, find, downloads, permission prompts, full screen, printing, extensions, and developer tools.
- Chromium's pop-up as it is. It has a read-only address, and sometimes none.

Consequences: sites' own pop-ups are the same kind of window, so everything uw adds checks its mark. uw draws the regular window's card, theme, and toolbar in a pop-up, where today pop-ups keep Chromium's layout. Chromium brings pop-ups back after a quit, so uw stops that for little windows.
