# The command bar replaces typing in the address bar

The user wants one keyboard entry point for commands, tasks, tabs, and the web, in the style of Arc's command bar and Raycast. On 2026-10-14 we decided that uw's own [command bar](../CONTEXT.md#language) opens from Cmd+T, Cmd+L, a click on the address, and Cmd+Shift+A, and that Chromium's address bar is no longer typed into. Cmd+T creates no tab until a page is picked, so a command typed there acts on the tab the user was on. Cmd+T is reserved by Chromium, so no website's own palette competes for it, as one would for Cmd+K.

The command bar gets history, bookmarks, open tabs, and search suggestions from Chromium's searchbox handler, the code behind the new tab page's search box. It does not rebuild them.

Considered options: commands added to the address bar's suggestions (rejected: the user wanted a panel of uw's own, with result menus and fallbacks); Chromium's tab search popup extended with commands (rejected for the same reason); a command bar on a separate key with Cmd+T unchanged (rejected: Cmd+T would still make a blank tab first, and most free keys clash with sites); Cmd+K (rejected: many sites use it for their own palette).

Consequences: every Chromium update can move the address bar and tab search code that uw reroutes. The address bar's own habits, such as completing an address inline, have to be kept on purpose in the command bar. The [plan](../plans/command-bar.md#open-questions) lists them.
