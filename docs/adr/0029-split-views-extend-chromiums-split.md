# Split views extend Chromium's split

Chromium's split view shows two tabs, side by side or stacked. On 2026-11-04 the user asked for any number of [panes](../CONTEXT.md#language), split right or down as in a terminal, and rearranged by dragging. We decided to patch Chromium's split so that one split holds any number of tabs and a layout tree, instead of one direction and one ratio. Each pane stays a tab, so tab search, the organizer, the agent's tab tools, restore, and closing keep working on tabs.

Considered options:

- A pane layout of uw's own, drawn in place of Chromium's page area, with Chromium's split left at two tabs. Chromium's updates would conflict less, but uw would rebuild the drop zones, mini toolbars, restore, and sidebar handling Chromium already has, and two kinds of split would exist side by side.
- Splits nested inside splits. Chromium's tab collections do not allow a split inside a split, so this changes as much of Chromium and still needs a tree.
- Panes inside one tab, as a terminal has them. Chromium assumes one page per tab everywhere.

Consequences: about 80 Chromium files read splits, and several check for exactly two tabs. Chromium is still changing split view, so each Chromium update can conflict with the patch.
