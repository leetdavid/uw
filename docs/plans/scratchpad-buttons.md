# Scratchpad buttons: Tidy and Clear

Status: scope agreed on 2026-10-24, and the designs, T165, signed off the same day.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Clear alone is 7/10: it closes tabs the way Close Branch does, but the Scratchpad heading is painted text today and must become a view inside Chromium's tab container to hold buttons. Tidy is 5/10: a new request over many tabs, a plan with several tasks applied as one undo, and answer quality that has not been measured.

The user asked for Arc's Clear and Tidy on 2026-10-24. [Tidy](../CONTEXT.md#language) replaces the "Reorganize all tabs" button the [product doc](../product.md#automatic-tab-organization) had agreed: it covers the Scratchpad only.

## Agreed scope

### Both buttons

- "Tidy" and "Clear" show at the right end of the Scratchpad heading while the pointer is over the heading, as Arc shows Clear on its divider. The heading shows only while the Scratchpad has tabs.
- The Tasks menu in the menu bar has "Tidy Scratchpad" and "Clear Scratchpad", so the keyboard and VoiceOver reach both. Neither has a key.
- Both act on the window they are in.

### Clear

- Closes every Scratchpad tab except [kept tabs](../CONTEXT.md#language) and [busy terminals](../CONTEXT.md#language). A chat's tab closes too, since the chat is saved.
- It asks nothing first. ⇧⌘T reopens the tabs one per press, as after Close Branch.

### Tidy

- Tidy asks the language model once about every Scratchpad tab. Each tab is sent with its title, its address, and the tab it was opened from; a terminal tab with its title and folder. The request also carries each task's name and its tabs' titles. It sends no page text, unlike the automatic requests in [ADR 0001](../adr/0001-classification-requests-include-page-excerpts.md), to keep one request over many tabs small and fast.
- The answer is a plan: which tabs join which existing task, which new tasks start and their names, and which tabs stay. uw carries it out as one [organization action](../CONTEXT.md#language), so one Undo or ⌥⌘Z puts every tab back. New tasks come after every other task, and their names are [generated names](../CONTEXT.md#language).
- Each tab is placed on its own, by the [branch rules](automatic-organization.md#branch-moves): a child placed with its parent stays nested under it, a child placed elsewhere leaves it, and a child nested by hand follows its parent.
- The user asked, so the [thresholds](../CONTEXT.md#language) do not apply. A new task needs at least two tabs. A lone tab that fits no task stays in the Scratchpad, and so do [hubs](automatic-organization.md#hub-pages) and kept tabs.
- Tidy runs while a language model is set, whether automatic organization is on, paused, or off. Clicking it is the go-ahead for that one request. With no language model, the button opens AI providers, as the Organize card's Set Up does. Private windows do not show it.
- While the request is out:
  - The Scratchpad's tabs stay where they are, and Tidy is grayed.
  - The activity card says "Organizing 9 tabs" with a spinner.
  - The automatic organizer holds back its answers about Scratchpad tabs.
  - A tab the user closes, moves, or opens a new page in is left out of the plan. The rest still applies.
- Then every tab moves at once, and the card says "Organized 9 tabs into 3 tasks" with Undo. When nothing moves, it says "Nothing to organize". On failure it says "Couldn't organize" with the HTTP status or network error and Try Again. The card shows all three with Show Activity off, because the user asked.
- Tidy's moves are uw's, not the user's: the automatic organizer may still move those tabs later. Only the user's own moves hold, as today.

## Design

Drawn on 2026-10-24 on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Automatic organization":

- "Scratchpad buttons: the sidebar": the pointer on Clear, with its tooltip "Close 4 tabs".
- "Scratchpad buttons: every state": the heading at rest and under the pointer, after Clear, Tidy asking, after Tidy, nothing to organize, a failure, no language model, and the pointer on the heading and after Tidy in dark mode.

Tidy's icon is sparkles and Clear's is a down arrow, as in Arc. Each button is 22px tall with 11px text, in the tint's text color at about 78%, and takes the tint's fill under the pointer.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T165: the designs, signed off by the user.
2. T166: the heading as a view, with Clear.
3. T167: Tidy.

## Acceptance checks

1. The heading shows Tidy and Clear only while the pointer is over it. Both are in the Tasks menu.
2. Clear closes every Scratchpad tab but kept tabs and busy terminals, and leaves every task alone. ⇧⌘T reopens a cleared tab.
3. Against a test server, Tidy sends one request with each Scratchpad tab's title, address, and parent, and the tasks with their tabs' titles. It applies the answer as one action, and one Undo puts every tab back.
4. A new task in the answer with one tab is not started. Hubs and kept tabs stay in the Scratchpad.
5. A child placed with its parent stays nested. A child placed elsewhere leaves it. A child nested by hand follows its parent.
6. A tab that changed while the request was out stays where it is, and the rest of the plan applies. No automatic answer moves a Scratchpad tab while Tidy waits.
7. Tidy works while organization is paused or off. With no language model it opens AI providers. A private window shows no Tidy.
8. The activity card shows asking, done with Undo, nothing to organize, and failure with Try Again, with Show Activity off.
9. Headless screenshots of the heading and the cards match the frames in light and dark mode.
10. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

None.
