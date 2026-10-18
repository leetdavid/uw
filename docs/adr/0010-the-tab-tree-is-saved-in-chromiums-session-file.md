# The tab tree is saved in Chromium's session file

uw saves each tab's place in the tab tree (its parent, collapse, and task name) as per-tab data in Chromium's session file. Chromium's session restore brings each page back with its history, scroll position, and form fields, so the tree data rides along with the tab it describes and needs no matching. Chromium drops that data whenever it rewrites the file, so a small patch keeps it. Decided on 2026-10-18, for now.

Considered options: a sidebar file of uw's own, as Arc keeps `StorableSidebar.json` (rejected for now: uw would have to match each restored tab to its record with no stable id between them, which Chromium's restore already does).

Consequences: moving to a file of uw's own later means carrying the saved tree data across once.
