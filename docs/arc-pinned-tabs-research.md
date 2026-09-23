# Arc pinned tabs research

Checked 2026-09-21. Official desktop documentation and release notes; no running-app test. Historical release notes establish documented changes, not verified behavior in today's build.

## Favorites, Pinned Tabs, and the saved URL

- Arc's **Favorites** are icons at the very top of the sidebar, above Space titles, with a documented limit of 12. Arc describes them as Pinned Tabs accessible in every Space. This is the closer visual reference for uw's requested compact icons. [Favorites][favorites]
- Ordinary **Pinned Tabs** belong to one Space, sit above the sidebar divider, support folders, and never auto-archive. [Pinned Tabs][pinned]
- The Profiles guide qualifies "every Space": Favorites belong to a Profile, alongside logins, cookies, history, and extensions. One Profile can serve multiple Spaces; different Profiles separate this data. [Profiles][profiles]
- "Home" means the saved pinned URL, initially the link pinned or favorited. It can be a specific subpage, not just the site's root. Navigation can leave that URL without replacing the saved target. [Pinned Tabs][pinned], [Original URL FAQ][original]
- Right-click > "Edit Pinned Page" > "Replace Pinned URL with Current" changes the saved target to the current URL; "Edit…" allows manual editing. This applies to Pinned Tabs and Favorites. [Original URL FAQ][original]
- Arc's Peek guide says a link clicked from a Pinned or Favorited Tab opens in Peek rather than leaving the pin. The preview can promote to a regular tab and closes independently. The user confirmed this as uw's default behavior. [Peek guide][peek]

## Reset triggers and limits of the evidence

| Action | What the sources establish |
| --- | --- |
| Click an ordinary Pinned Tab's favicon | Returns to its original pinned URL on macOS and Windows. A slash beside its name signals navigation away from that URL. This is specifically a favicon click, not a promise about every row-selection click. [Slash FAQ][slash] |
| Single-click a Favorite | The reviewed Favorites guide does not specify whether selection also resets an already-open page. Do not generalize the ordinary Pinned Tab favicon rule to this whole icon tile. [Favorites][favorites], [Pinned Tabs][pinned] |
| Double-click a Favorite | Explicitly resets to its pinned URL in Windows v0.2.1, 2023-12-18. The reviewed macOS sources do not establish the equivalent gesture. [Windows releases][windows-releases] |
| Double-click a named tab | macOS v1.33.0, 2024-03-07, documents double-click or right-click to rename a tab. This is distinct from the Windows Favorite reset gesture. [macOS releases][mac-releases] |
| Command-bar reset | The Pinned Tabs guide documents Command-T, then "Reset Tab". It does not describe a separate Favorite-specific command. [Pinned Tabs][pinned] |
| Command-click a Pinned Tab's icon | macOS v1.31.0, 2024-02-22, returns to the saved URL and opens the previous URL in a new tab. The note does not promise preservation of that page's live state. [macOS releases][mac-releases] |
| Context-menu action | Editing the saved target is documented above. A dedicated context-menu reset action is not established by these guides. [Original URL FAQ][original], [Pinned Tabs][pinned] |
| Close button / Cmd-W / Ctrl-W | The shortcut guide says "Close current tab or window". Windows v0.2.1 added a Pinned Tab close button. Neither specifies whether home-reset happens on close or on next open, or exactly when the page unloads. [Shortcuts][shortcuts], [Windows releases][windows-releases] |
| Cmd-W while Peek is open | Closes the Peek overlay. The guide describes Peek as previewing a linked page without leaving the source pin; this does not establish the behavior when the Favorite itself is the close target. [Peek guide][peek] |
| Reopen a pin / Cmd-Shift-T / Ctrl-Shift-T | The shortcut guide documents reopening the last closed tab, without pin-specific URL or live-state guarantees. The FAQ's "always revert" wording supplies no exact lifecycle trigger. [Shortcuts][shortcuts], [Original URL FAQ][original] |
| Restart Arc | macOS v1.42.0, 2024-05-09, added browsing-state restoration for all users. It does not specify whether a navigated Favorite or pin restores its current URL or home URL in each case. [macOS releases][mac-releases] |

## Windows, profiles, and live state

- On macOS and Windows, a tab created in a Space appears in that same Space across Arc windows. "New Blank Window" creates a separate window; dragging a tab out also creates one. The FAQ specifies tab presence, not shared page execution. [Multiple windows FAQ][windows]
- macOS v0.106, 2023-05-31, explicitly retained pinned-tab sync across windows while making unpinned tabs window-specific. The 2023-08-24 notes restored unpinned-tab sync. Older descriptions of window behavior can therefore differ. [2023 macOS releases][mac-2023]
- Favorites need not stay loaded: the 2023-02-09 macOS notes changed them to load based on recent use. v0.108, 2023-06-15, fixed audio and picture-in-picture continuing after closing a Favorite. Neither note defines home-reset timing. [2023 macOS releases][mac-2023]
- Device sync is a separate scope. Arc Sync documents syncing Spaces, Folders, and Tabs, but explicitly excludes Favorites, Profiles, browsing history, and saved passwords. Its guide says legacy iCloud sync was deprecated with macOS v1.48.0, 2024-06-20. [Arc Sync][sync]
- **Shared live state is not established.** These sources document shared sidebar entries and profile-scoped cookies/logins. They establish neither shared nor independent live navigation, scroll position, unsent forms, media playback, or JavaScript state across windows. "Sync" is not evidence of shared Chromium `WebContents` or a mirrored DOM. [Multiple windows FAQ][windows], [Profiles][profiles], [Arc Sync][sync]

## User-confirmed behavior

The user reports that Cmd-W does nothing on a pinned tab in Arc, and that double-clicking the pin or using its context menu returns it home. These are the agreed [uw interactions](product.md#pinned-tabs). They are user observations, not results of a local Arc runtime test.

The earlier assistant proposal that Cmd-W globally closes the shared session and resets home on next open was rejected.

## Remaining verification and decisions

The user has confirmed cross-window page continuity. The evidence above remains a documentation review of Arc. These points still need resolution:

1. The user requires the live pin to transfer to the selecting window, leaving a hazed inactive view in the previous window. Verify Arc's equivalent behavior before treating it as a direct product match.
2. The user requires unpinning to remove shared pin views and retain the live page as a regular tab in its current window. Closing its owning window detaches the live pin session, and it remains alive even after the final browser window closes while uw runs. After a quit, uw opens a new tab and offers restoration. Accepting restores pins with the session, while dismissing leaves sidebar pins available. Cmd-W is already specified as a no-op, and double-click/context-menu home reset is agreed. The documented Peek close gesture is a separate case.
3. How does editing a home URL affect an existing live view: navigate it immediately or change only future resets?

[favorites]: https://resources.arc.net/hc/en-us/articles/19230755904151-Favorites-Top-Tabs-Across-Every-Space
[pinned]: https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around
[profiles]: https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing
[original]: https://resources.arc.net/hc/en-us/articles/25541939922199-Why-Are-My-Pinned-Tabs-and-Favorites-Reverting-Back-to-Their-Original-URL-in-Arc-for-Desktop
[slash]: https://resources.arc.net/hc/en-us/articles/25625148480279-Why-is-There-a-Slash-Next-to-My-Pinned-Tab-s-Name
[shortcuts]: https://resources.arc.net/hc/en-us/articles/20595231349911-Keyboard-Shortcuts
[peek]: https://resources.arc.net/hc/en-us/articles/19335302900887-Peek-Preview-Sites-From-Pinned-Tabs
[windows]: https://resources.arc.net/hc/en-us/articles/25590417429783-Why-Are-the-Same-Tabs-Appearing-Across-Multiple-Arc-Windows
[sync]: https://resources.arc.net/hc/en-us/articles/20272860828823-Arc-Sync
[windows-releases]: https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes
[mac-releases]: https://resources.arc.net/hc/en-us/articles/20498293324823-Arc-for-macOS-2024-2026-Release-Notes
[mac-2023]: https://resources.arc.net/hc/en-us/articles/20498377604887-Arc-for-macOS-2023-Release-Notes
