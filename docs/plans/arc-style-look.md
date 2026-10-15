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
- **Text on the tint.** Text and icons are near-black or white, whichever has more contrast with the tint. Fills and the active tab change strength the same way.
- **Readable tints.** Text keeps at least a 4.5:1 contrast ratio everywhere on the tint. A mid-tone color cannot do that, so uw makes it lighter or darker, whichever takes the smaller change, and keeps its hue.
- **Default tints.** Lilac to sky in light mode, indigo to slate in dark mode, until the user picks a color.
- **Page card.** The page and its toolbar sit in a card with an 8px inset, a 12px radius, and a soft shadow. The card follows light or dark mode, not the tint.
- **Tab rows.** 34px tall with a 10px radius. The active tab is a card. Other tabs have no background until hovered.
- **Favorites.** Pinned tabs are tiles, four per row, 44px tall with a 12px radius.
- **Tasks.** A tab group header is a folder row: a folder icon in the group's color, the name in semibold, and a chevron. It keeps Chromium's collapse, rename, color, and drag.
- **Sidebar foot.** The Organize button is a pill at the left, and the [new button](keyboard-and-mouse.md#new-button) sits at the right end. The automatic-move message is a small card above them. The new button was added on 2026-10-15.
- **Toolbar.** 44px tall, on the card.

## Non-goals

- Moving the address bar into the sidebar.
- Arc's Spaces and per-space themes.
- A color picker widget built by uw. The settings control uses preset colors and the browser's standard color input.
- New icons, a new app icon, or custom window buttons.
- Any change to how tabs, tasks, or the tab tree behave.

## Design

- **Colors.** `components/look/tint` holds the color rules, tested without a window. A uw color mixer in `browser/ui/look` sets Chromium's existing color IDs from them. It is added in `chrome/browser/ui/color/chrome_color_mixers.cc`. No view picks colors by itself.
- **Glass frame.** Chromium's transparent macOS frame hides every frame color, so uw turns it off by default. Its preference still works.
- **Tint control.** uw's settings page has a "Window tint" section: eight preset colors, a color input for any other color, and a button for the default. It says so when it had to adjust a color. The pick is stored as Chromium's theme color, so the "Customize Chromium" panel changes the same value.
- **Gradient.** `PaintTint` paints the gradient in window coordinates wherever Chromium paints the frame color, so every view draws one continuous sheet.
- **Sizes.** `chrome/browser/ui/layout_constants.cc` already has values for the vertical tab height, radius, padding, and pinned tab height. The patch changes those numbers.
- **Card.** The window layout insets the main area beside the vertical tab strip by `kPageCardMargins`, then reuses Chromium's own corner rounding for the toolbar and the page with `kPageCardRadius`. Fullscreen windows have no card.
- **Task rows.** `TabGroupHeaderView` is patched for the vertical strip: no fill, text in the tint's text color, and a folder icon where it shows the shared-group icon. Its click, drag, and editor code are untouched.
- **Screenshots.** `SaveWindowScreenshot` saves the composited window as a PNG from a headless test. See [the README](../../README.md#tooling-checks).

## Stages

1. **Screenshot helper.** Built. It saves the sidebar with a task in light and dark mode, an automatic move, and the settings page.
2. **Tint and card.** Built, except the shadow. The toolbar and the page sit in a card with an 8px inset and a 12px radius. The card has no shadow yet.
3. **Tab rows and favorites.** Built: 34px rows with a 10px radius, the active tab as a card, and pinned tabs as filled tiles. Tiles use the row radius of 10px, not 12px, because Chromium has one radius for both.
4. **Task rows.** Built. The header shows a folder icon in the group's color, the name in semibold, and Chromium's chevron. Chromium's colored line beside a task's tabs stays, although the design has none.
5. **Sidebar foot and toolbar.** The Organize and Undo buttons are pills. The automatic-move message is the [activity card](automatic-organization.md#agreed-scope), built on 2026-10-12. The toolbar keeps Chromium's height of about 46px.
6. **User tint.** Built. A color picked on uw's settings page drives the gradient and the text color, and a mid-tone pick is made readable.

## Acceptance checks

1. With a new profile, the screenshots match the Doop frames for the tab tree, automatic organization, settings, and tint pages in layout, sizes, and colors.
2. Tab and task behavior is unchanged: `TabOrganizationControllerBrowserTest.*` and `AutomaticOrganizerBrowserTest.*` pass without edits.
3. Collapse, rename, recolor, and drag of a task still work from the folder row.
4. With either default tint, text on the tint and on the card has at least a 4.5:1 contrast ratio.
5. With any picked tint, text on it has at least a 4.5:1 contrast ratio. `TintTest.*` covers light, dark, and mid-tone colors. `TintBrowserTest.*` covers a picked color in a window. `UwOrganizationPageBrowserTest.*` picks a preset, a mid-tone color, and the default from the settings page.
6. Horizontal tabs keep Chromium's layout. Their frame takes the tint as a plain color, like any Chromium theme color.
7. `pnpm run check` and `pnpm run verify` pass.

## Open questions

| Question | Options |
| --- | --- |
| Collapsed sidebar | The design shows only the expanded sidebar. The collapsed strip keeps Chromium's layout unless designed. |
