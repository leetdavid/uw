# Arc-style look

Status: proposed on 2026-10-11. Not approved. No code for it exists yet.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Sizes and colors are small changes because Chromium already has settings for them. The task folder row and the tint gradient need custom drawing. Chromium's vertical tab strip is new and changes with every release, and the result can only be judged by eye.

The look was [agreed](../product.md#minimum-usable-version). The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs). Its style guide `uw-arc-style` holds the palette and sizes used below.

## Scope

- **Window tint.** The frame, the sidebar, and the area around the page share one tint.
- **Page card.** The page and its toolbar sit in a white card with an 8px inset, a 12px radius, and a soft shadow.
- **Tab rows.** 34px tall with a 10px radius. The active tab is a white card. Other tabs have no background until hovered.
- **Favorites.** Pinned tabs are tiles, four per row, 44px tall with a 12px radius.
- **Tasks.** A tab group header is a folder row: a folder icon in the group's color, the name in semibold, and a chevron. It keeps Chromium's collapse, rename, color, and drag.
- **Sidebar foot.** The Organize button is a pill. The automatic-move message is a small white card above it.
- **Toolbar.** 44px tall, on the card.

## Non-goals

- Moving the address bar into the sidebar.
- Arc's Spaces, per-space themes, and a tint picker.
- New icons, a new app icon, or custom window buttons.
- Any change to how tabs, tasks, or the tab tree behave.

## Design

- **Colors.** A uw color mixer sets Chromium's existing color IDs. It is added with one line in `chrome/browser/ui/color/chrome_color_mixers.cc`. No view picks colors by itself.
- **Sizes.** `chrome/browser/ui/layout_constants.cc` already has values for the vertical tab height, radius, padding, and pinned tab height. The patch changes those numbers.
- **Card.** Chromium already rounds the page corners beside the vertical tab strip and has a shadow frame view. The patch sets the radius and inset and reuses both.
- **Task rows.** `TabGroupHeaderView` gets a vertical-mode drawing path in uw code, called from one patched line. Its click, drag, and editor code are untouched.
- **Screenshots.** A browser test saves the whole window as PNG files in headless mode. Looks are reviewed from those files, so no window opens on the desktop. See stage 1.

## Stages

Each stage would become a ticket on the [board](roadmap.md#board).

1. **Screenshot test.** Find out whether a headless browser test can capture the composited window. If it cannot, stop and choose another way to review looks before any restyling.
2. **Tint and card.** Colors, card inset, radius, and shadow.
3. **Tab rows and favorites.** Sizes, active and hover states, and tiles.
4. **Task rows.** The folder header.
5. **Sidebar foot and toolbar.** The Organize pill, the message card, and the toolbar height.

## Acceptance checks

1. With a new profile, the screenshots match the Doop frames for the tab tree, automatic organization, and settings pages in layout, sizes, and colors.
2. Tab and task behavior is unchanged: `TabOrganizationControllerBrowserTest.*` and `AutomaticOrganizerBrowserTest.*` pass without edits.
3. Collapse, rename, recolor, and drag of a task still work from the folder row.
4. Text keeps at least a 4.5:1 contrast ratio on the tint and on the card.
5. Horizontal tabs still look like stock Chromium.
6. `pnpm run check` and `pnpm run verify` pass.

## Open questions

| Question | Options |
| --- | --- |
| Tint | A fixed tint from the design, or the color of the user's Chromium theme. A fixed solid tint is the smallest first step. The design's two-color gradient needs custom drawing. |
| Dark mode | The design has only a light palette. Either light only for now, with dark mode keeping Chromium's stock look, or design a dark palette first. |
| Address bar | Stays in the toolbar on the card in this plan. Moving it into the sidebar is a separate decision. |
| Collapsed sidebar | The design shows only the expanded sidebar. The collapsed strip keeps Chromium's layout unless designed. |
