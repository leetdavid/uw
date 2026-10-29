# New tab page

Agreed on 2026-10-29. uw's [new tab page](../CONTEXT.md) replaces Chromium's: a box to search or ask, and the state of uw's features. It grew from the Ask box first proposed for T49 in the [agent chats plan](agent-chats.md). T263 draws it and T49 builds it.

## Where it shows

Every tab that would show Chromium's new tab page shows `uw://newtab` instead: "New blank tab" in the command bar, Cmd+T with the command bar off, New Child Tab, New Tab Below, a task's New Tab, and the first tab of a new window or workspace. Chromium's page, with its Google logo, account bar, shortcuts, and "Customize uw" button, no longer shows. The patch points `NewTabURLDetails::ForProfile` at it, and the page counts as a new tab page wherever Chromium checks, so the address bar stays empty and the box takes the focus.

Until it ships, Chromium's page stays, and Google's account bar on it keeps logging to Google. The user accepted that on 2026-10-29; see [Google requests](google-requests.md#kept).

## The box

The box follows the frame "New tab: Search or Ask": uw's logo, one box, and a Search | Ask switch, centered. The page follows light and dark mode, and its tab reads "New Tab".

- Search is on when the page opens. Return goes where the address bar would with the same text: an address opens, and other text searches with the default search engine, in this tab.
- While the user types, the box sends nothing to the search engine. Suggestions come only from open tabs, history, and bookmarks, as in the [command bar](command-bar.md).
- Ask turns this tab into a chat whose first message is the text, as Cmd+Return in the command bar does in a new tab. The box shows the chat page's model chip and the "Once" chip, which opens the schedule card. The agent's tabs open under it.
- Tab moves between Search and Ask. The switch keeps no memory between pages.

## Sections

Below the box, each section is hidden when it has nothing to show, so a quiet day shows only the box.

| Section | Shows |
| --- | --- |
| Needs you | Steps waiting for the user's approval, from any chat or run. |
| Finished | Chats and runs that ended since the user last looked, each with its one-line result. |
| Coming up | Scheduled chats and watched pages, with their next run. |
| Running | Busy terminals, and what plays in the mini player. |
| Tabs | Whether automatic organization is on or paused, and recent automatic moves with Undo. |
| Setup | What is broken: a provider failing or missing a key, a connector that needs sign-in, or an update ready to install. |

How each row looks and what a click on it does is decided in the design round.

## Private windows

A private window's new tab page shows only the box, and Ask starts a [private chat](../CONTEXT.md). It shows no sections, so none of the profile's saved activity appears there.

## Stages

1. T263: the design on the canvas. The page with every section, each section's states, the private window's page, and the empty page. The user signs it off before T49 starts.
2. T49: the page, in every place above, with all six sections. The [Scheduled page](../product.md#scheduled-chats) (T238) and kept alerts (T239) supply Coming up and Finished.

Implementability 5/10. The box reuses the chat page's composer and the command bar's suggestions. The risk is in Chromium's many special cases for its own new tab page, and in six sections that read from as many parts of uw and must stay current while the page is open.
