# Arc-style look

Status: approved on 2026-10-11 with the decisions below. Stage 1 and the tint half of stage 2 are built.

Implementability: **4/10** using the [roadmap convention](roadmap.md#implementability-estimates). Sizes and colors are small changes because Chromium already has settings for them. The tint gradient, the user's own tint, and the task folder row need custom drawing in several views. Chromium's vertical tab strip is new and changes with every release, and the result can only be judged by eye.

The look was [agreed](../product.md#minimum-usable-version). The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), with the tint on its "Tint" page. The style guide `uw-arc-style` holds the palettes, sizes, and tint rules used below.

## Decisions

Agreed on 2026-10-11:

- The tint is a two-color gradient.
- The user can change the tint. The color they pick decides whether text on the tint is white or near-black.
- There is a dark default palette as well as the light one.
- The address bar stays in the toolbar on the page card for now.
- Looks are reviewed from screenshots saved by headless tests.

## Scope

- **Window tint.** The frame, the sidebar, and the area around the page share one gradient. It runs from the picked color to the same color with its hue turned by 40 degrees.
- **Text on the tint.** Text and icons are near-black or white, whichever has more contrast with the tint. Fills and the active tab change strength the same way.
- **Default tints.** Lilac to sky in light mode, indigo to slate in dark mode, until the user picks a color.
- **Page card.** The page and its toolbar sit in a card with an 8px inset, a 12px radius, and a soft shadow. The card follows light or dark mode, not the tint.
- **Tab rows.** 34px tall with a 10px radius. The active tab is a card. Other tabs have no background until hovered.
- **Favorites.** Pinned tabs are tiles, four per row, 44px tall with a 12px radius.
- **Tasks.** A tab group header is a folder row: a folder icon in the group's color, the name in semibold, and a chevron. It keeps Chromium's collapse, rename, color, and drag.
- **Sidebar foot.** The Organize button is a pill. The automatic-move message is a small card above it.
- **Toolbar.** 44px tall, on the card.

## Non-goals

- Moving the address bar into the sidebar.
- Arc's Spaces and per-space themes.
- A color picker built by uw. See the open question on how the user picks.
- New icons, a new app icon, or custom window buttons.
- Any change to how tabs, tasks, or the tab tree behave.

## Design

- **Colors.** `components/look/tint` holds the color rules, tested without a window. A uw color mixer in `browser/ui/look` sets Chromium's existing color IDs from them. It is added in `chrome/browser/ui/color/chrome_color_mixers.cc`. No view picks colors by itself.
- **Glass frame.** Chromium's transparent macOS frame hides every frame color, so uw turns it off by default. Its preference still works.
- **Tint source.** Proposed: read the color the user already picks in Chromium's "Customize Chromium" panel. That reuses Chromium's picker and its storage.
- **Gradient.** `PaintTint` paints the gradient in window coordinates wherever Chromium paints the frame color, so every view draws one continuous sheet.
- **Sizes.** `chrome/browser/ui/layout_constants.cc` already has values for the vertical tab height, radius, padding, and pinned tab height. The patch changes those numbers.
- **Card.** Chromium already rounds the page corners beside the vertical tab strip and has a shadow frame view. The patch sets the radius and inset and reuses both.
- **Task rows.** `TabGroupHeaderView` gets a vertical-mode drawing path in uw code, called from one patched line. Its click, drag, and editor code are untouched.
- **Screenshots.** `SaveWindowScreenshot` saves the composited window as a PNG from a headless test. See [the README](../../README.md#tooling-checks).

## Stages

1. **Screenshot helper.** Built. It saves the sidebar with a task in light and dark mode, an automatic move, and the settings page.
2. **Tint and card.** The default gradients and the text color on the tint are built. The card inset, radius, and shadow are not.
3. **Tab rows and favorites.** Sizes, active and hover states, and tiles.
4. **Task rows.** The folder header.
5. **Sidebar foot and toolbar.** The Organize pill, the message card, and the toolbar height.
6. **User tint.** The picked color drives the gradient and the text color.

## Acceptance checks

1. With a new profile, the screenshots match the Doop frames for the tab tree, automatic organization, settings, and tint pages in layout, sizes, and colors.
2. Tab and task behavior is unchanged: `TabOrganizationControllerBrowserTest.*` and `AutomaticOrganizerBrowserTest.*` pass without edits.
3. Collapse, rename, recolor, and drag of a task still work from the folder row.
4. With either default tint, text on the tint and on the card has at least a 4.5:1 contrast ratio.
5. With a picked tint, text on it is whichever of near-black and white has more contrast. A unit test covers light, dark, and mid-tone colors.
6. Horizontal tabs keep Chromium's layout. Their frame takes the tint as a plain color, like any Chromium theme color.
7. `pnpm run check` and `pnpm run verify` pass.

## Open questions

| Question | Options |
| --- | --- |
| How the user picks the tint | Proposed: Chromium's own color picker in the "Customize Chromium" panel. The other option is a control on uw's settings page. |
| Mid-tone tints | Some picked colors leave both text colors under 4.5:1. Either accept that, or nudge such a tint lighter or darker until text reaches 4.5:1. |
| Collapsed sidebar | The design shows only the expanded sidebar. The collapsed strip keeps Chromium's layout unless designed. |
