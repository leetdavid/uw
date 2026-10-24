# Arc-style look

Status: approved on 2026-10-11 with the decisions below. Stages 1 to 4 and 6 are built, except the card's shadow. Stage 5 is built except the toolbar height.

Implementability: **4/10** using the [roadmap convention](roadmap.md#implementability-estimates). Sizes and colors are small changes because Chromium already has settings for them. The tint gradient, the user's own tint, and the task folder row need custom drawing in several views. Chromium's vertical tab strip is new and changes with every release, and the result can only be judged by eye.

The look was [agreed](../product.md#minimum-usable-version). The design is on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), with the tint on its "Tint" page. The style guide `uw-arc-style` holds the palettes, sizes, and tint rules used below.

## Decisions

Agreed on 2026-10-11:

- The tint is a two-color gradient.
- The user can change the tint. The color they pick decides whether text on the tint is white or near-black.
- There is a dark default palette as well as the light one.
- The address bar stays in the toolbar on the page card for now.
- Looks are reviewed from screenshots saved by headless tests.

Agreed on 2026-10-12:

- The user picks the tint with a control on uw's settings page.
- A picked color that would leave text hard to read is made readable.

## Scope

- **Window tint.** The frame, the sidebar, and the area around the page share one gradient. It runs from the picked color to the same color with its hue turned by 40 degrees.
- **Text on the tint.** Text and icons are near-black or white, whichever has more contrast with the tint. Fills and the active tab change strength the same way. Fills are see-through white, 46% on a light tint and 8% on a dark one, so the gradient shows through tiles, pills, and hovered rows.
- **Readable tints.** Text keeps at least a 4.5:1 contrast ratio everywhere on the tint. A mid-tone color cannot do that, so uw makes it lighter or darker, whichever takes the smaller change, and keeps its hue.
- **Default tints.** Lilac to sky until the user picks a color. Changed on 2026-10-16: dark mode is black whatever color is picked, and the [themes plan](themes.md) makes the tint a theme of up to three colors.
- **Page card.** The page and its toolbar sit in a card with an 8px inset, a 12px radius, and a soft shadow. The card follows light or dark mode, not the tint.
- **Tab rows.** 34px tall with a 10px radius. The active tab is a card. Other tabs have no background until hovered.
- **Favorites.** Pinned tabs are tiles, four per row, 44px tall with a 12px radius.
- **Tasks.** A tab group header is a folder row: a folder icon in the group's color and the name in semibold. While the pointer is on the row, a chevron takes the folder icon's place, as on a [parent tab](keyboard-and-mouse.md#branches-with-the-mouse). It keeps Chromium's collapse, rename, color, and drag. Changed on 2026-10-16: the chevron used to sit at the end of the row.
- **Sidebar foot.** The Organize button is a pill at the left, and the [new button](keyboard-and-mouse.md#new-button) sits at the right end. The automatic-move message is a small card above them. The new button was added on 2026-10-15.
- **Toolbar.** 44px tall, on the card.

## Non-goals

- Moving the address bar into the sidebar.
- Arc's Spaces and per-space themes.
- A color picker widget built by uw. The settings control uses preset colors and the browser's standard color input.
- New icons, a new app icon, or custom window buttons. Changed on 2026-10-24: the [floating sidebar](hidden-sidebar.md) draws its own.
- Any change to how tabs, tasks, or the tab tree behave.

## Design

- **Colors.** `components/look/tint` holds the color rules, tested without a window. A uw color mixer in `browser/ui/look` sets Chromium's existing color IDs from them. It is added in `chrome/browser/ui/color/chrome_color_mixers.cc`. No view picks colors by itself.
- **Glass frame.** Chromium's Liquid Glass frame shows only where the theme asks for it, per [Glass](themes.md#glass). Chromium's own switch for it no longer does anything.
- **Tint control.** Replaced on 2026-10-16 by the [theme picker](themes.md). uw's settings page has a `Theme` row that opens it. The theme's first color is stored as Chromium's theme color, so the "Customize Chromium" panel changes the same value.
- **Gradient.** `PaintTint` paints the gradient in window coordinates wherever Chromium paints the frame color, so every view draws one continuous sheet.
- **Sizes.** `chrome/browser/ui/layout_constants.cc` already has values for the vertical tab height, radius, padding, and pinned tab height. The patch changes those numbers.
- **Card.** The window layout insets the main area beside the vertical tab strip by `kPageCardMargins`, then reuses Chromium's own corner rounding for the toolbar and the page with `kPageCardRadius`. Fullscreen windows have no card.
- **Collapsed sidebar.** To be replaced by the [hidden sidebar](hidden-sidebar.md), agreed on 2026-10-24. Chromium starts the narrow sidebar below the window's close, minimize, and zoom buttons and runs the toolbar beside them. uw starts the card there too, level with the narrow sidebar, so the buttons sit on the tint and the toolbar and the page share the card's left edge. Chosen by uw on 2026-10-22 after the user reported a light block behind the buttons. A narrow sidebar wide enough to hold the buttons is the alternative. A task's row there shows only its folder: a name with no room for two letters goes, rather than leaving a sliver of its first one.
- **Task rows.** `TabGroupHeaderView` is patched for the vertical strip: no fill, text in the tint's text color, and a folder icon where it shows the shared-group icon. Chromium's collapse chevron sits beside the folder icon and replaces it under the pointer. Its click, drag, and editor code are untouched.
- **Screenshots.** `SaveWindowScreenshot` saves the composited window as a PNG from a headless test. See [the README](../../README.md#tooling-checks).

## Stages

1. **Screenshot helper.** Built. It saves the sidebar with a task in light and dark mode, an automatic move, and the settings page.
2. **Tint and card.** Built, except the shadow. The toolbar and the page sit in a card with an 8px inset and a 12px radius. The card has no shadow yet.
3. **Tab rows and favorites.** Built: 34px rows with a 10px radius, the active tab as a card, and pinned tabs as filled tiles. Tiles use the row radius of 10px, not 12px, because Chromium has one radius for both.
4. **Task rows.** Built. The header shows a folder icon in the group's color and the name in semibold. Under the pointer, the chevron takes the folder icon's place. Chromium's colored line beside a task's tabs stays, although the design has none.
5. **Sidebar foot and toolbar.** The Organize and Undo buttons are pills. The automatic-move message is the [activity card](automatic-organization.md#agreed-scope), built on 2026-10-12. The toolbar keeps Chromium's height of about 46px.
6. **User tint.** Built. A color picked on uw's settings page drives the gradient and the text color, and a mid-tone pick is made readable.

## Acceptance checks

1. With a new profile, the screenshots match the Doop frames for the tab tree, automatic organization, settings, and tint pages in layout, sizes, and colors.
2. Tab and task behavior is unchanged: `TabOrganizationControllerBrowserTest.*` and `AutomaticOrganizerBrowserTest.*` pass without edits.
3. Collapse, rename, recolor, and drag of a task still work from the folder row.
4. With either default tint, text on the tint and on the card has at least a 4.5:1 contrast ratio.
5. With any picked tint, text on it has at least a 4.5:1 contrast ratio. `TintTest.*` covers light, dark, and mid-tone colors. `TintBrowserTest.*` covers a picked color in a window. `ThemePickerBrowserTest.*` picks a suggested theme, moves and adds dots, and sets intensity, grain, and the mode from the picker.
6. Windows without a sidebar, such as pop-ups, keep Chromium's layout. Their frame takes the tint as a plain color, like any Chromium theme color.
7. `pnpm run check` and `pnpm run verify` pass.
8. With the sidebar collapsed, the window buttons sit on the tint and the card is one rounded rectangle level with the narrow sidebar. A task's row shows only its folder. `WindowScreenshotBrowserTest.TheNarrowSidebarLeavesTheWindowButtonsOnTheTint` checks the layout headless.
