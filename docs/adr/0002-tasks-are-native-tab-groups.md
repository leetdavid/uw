# Tasks are native tab groups

The first tab tree drew uw-owned task containers inside Chromium's vertical tab container and overrode its layout. All four native crashes fixed in that code came from this design, and it gave the sidebar two grouping systems, since Chromium's own tab groups still existed beside uw's tasks. On 2026-10-11 we decided that a task is a native Chromium tab group. uw adds only what Chromium lacks: page tabs nested under their parent page, the Scratchpad label for ungrouped tabs, the Organize button, and the organizer.

This drops one agreed requirement: tasks no longer nest inside tasks, because Chromium's tab groups cannot contain groups. Automatic organization only ever created top-level tasks, so nesting was reachable by hand only.

Considered options: a uw-owned native container replacing Chromium's unpinned container (rejected: uw would own layout, drop targets, and focus order for every tab row); a WebUI sidebar on Chromium's tab strip service (rejected: it conflicts with native vertical tabs and loses hover cards, tear-off drag, and tab menus).
