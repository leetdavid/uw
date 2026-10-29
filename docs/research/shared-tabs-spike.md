# Shared tabs spike

Run on 2026-10-29 for ticket T252 of the [shared tabs plan](../plans/shared-tabs.md#stages), against Chromium 153.0.8010.53. The question: can uw copy a workspace's tabs into every window that shows it, as `PinSync` copies pins, and what does that cost?

## Result

Yes. A copy costs about what an unloaded Chromium tab costs, a task stays in step across windows through Chromium's own tab group calls, and Memory Saver's rules still work for a page held in no window. The main cost is what the rest of Chromium sees: extensions, recently closed, tab search, and the command bar see each tab once per window unless uw hides the copies.

The checks were `SharedTabsSpikeBrowserTest.*`, a headless browser test added in b2670ea and removed in the next commit. The numbers are from one run on the user's Mac. Headless windows build their views but draw nothing on screen.

## Cost

A copy is a page with the tab's history and no renderer, in a tab of its own window.

| What | Measured |
| --- | --- |
| 100 unloaded tabs in one window | +37.5 MB |
| One copy | about 345 KB, close to an unloaded tab |
| 4 windows on a workspace of 100 tabs, each with a hidden partner on a second workspace of 100 tabs | 808 tabs, 606 of them copies. +330 MB in the browser process, about 210 MB of it copies |
| Inserting 303 copies | 1.8 to 1.9 s, about 6 ms each, on the main thread |
| A new tab reaching 3 other windows | 26 ms |
| Moving a live page between windows | 6 ms, with half-typed input kept |

- Copies run no renderer, so the cost stays in the browser process.
- Filling a new window on a workspace of 100 tabs takes about 0.6 s. T253 should show the window first and fill it after, the selected tab first.
- Hidden partners multiply the copies: a window holds one for each workspace the user has switched to in it. Since closing a window would lose no tabs, a partner hidden for a while could close and open again when switched to. That is a proposal, not tried.

## What the rest of Chromium sees

| Where | What it sees | How found |
| --- | --- | --- |
| Extensions' `chrome.tabs.query` | Every copy, as a tab with status `unloaded` and `discarded` false, like a restored tab not loaded yet. 3 tabs in 2 windows came back as 7. | Measured |
| Memory Saver on a copy | Never unloads it: "Tab doesn't have a main frame". | Measured |
| Memory Saver on a page in no window | Its rules still apply. A page counting in its title stayed protected after it left every window. Chromium counts title changes only once a page has loaded and been quiet for a second; asked sooner, it called the same page eligible. | Measured |
| Recently closed | Closing a window records it with all its tabs, copies included. | Measured |
| Chromium's saved tab groups | One saved group for each window's copy of a task, so 2 for one task in 2 windows. uw already hides that menu and clears closed groups (T86). | Measured |
| Tab search | Lists every window's tabs. Copies of one task have different group ids, so nothing merges them. | Read in `TabSearchPageHandler::CreateProfileData` |
| The command bar's open-tab rows | Come from every window. The address bar merges rows with one URL, but "switch to tab" can pick any window's copy. | Read in `TabMatcherDesktop::GetOpenTabs` |
| Synced open tabs | Nothing: uw is built without Google API keys, so Chrome Sync never runs. | Read in `chromium/args.gn` |
| The session file | Each window saves its tabs, copies included. A copy needs a mark to come back as a copy, as `uw.pin_placeholder` marks a pin's. | Known from T133 |

## Tasks

- A task stayed in step across two windows through Chromium's own calls, `AddToNewGroup`, `ChangeTabGroupVisuals`, `AddToExistingGroup`, `MoveGroupTo`, `MoveWebContentsAt`, and `RemoveFromGroup`, after each of these: a new task, a rename and a new color, collapse and expand, a tab joining, the task moving to the top, a tab reordered inside it, a tab leaving, and its last tabs leaving.
- The copy has to move a task as a whole with `MoveGroupTo` before it places single tabs. Moving the task's tabs to the top one at a time split it.
- Collapsing a task in one window left another window's selected tab inside it. Chromium allowed that and kept the selection. T253 has to decide what that window shows.

## Not tried

- One page tree, organizer, undo, and set of manual adjustments per workspace. `TabOrganizationController` keeps one per window today, and copies arrive in it as new tabs with no parent.
- Hiding copies from extensions, tab search, and the command bar.
- Leaving a closed window out of recently closed.
- Copies in the session file, and the first-launch merge.
- A real drag between two windows on one workspace, which should move the live page and not the tab.
