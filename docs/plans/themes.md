# Themes

Status: scope agreed on 2026-10-16, and the designs, T93, signed off the same day. T94 to T97, the saved theme, black dark mode, the picker, and grain, are built and in review. Nobody has used them in a real window yet.

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Gradients, black dark mode, and the saved theme reuse the tint code that exists. The picker is the risk: a color field with dots to drag is a custom control.

The look of the window is in the [Arc-style look plan](arc-style-look.md). This plan replaces its single tint color with a [theme](../CONTEXT.md#language), in the way Arc's theme picker works.

## Agreed scope

- A profile has one theme, shared by all its windows. Each profile can look different.
- The picker opens over the window, beside the sidebar, from "Theme…" in the menu of empty sidebar space and from View → Theme… in the menu bar. The window changes as the user picks. The "Window tint" section of uw's settings page becomes one row that opens the picker.
- The picker has, from the top:
  - Light, Dark, and Auto. Auto follows macOS. This is Chromium's own color mode setting, which web pages and uw's pages already follow.
  - Suggested themes.
  - A color field with up to three dots to drag. One dot gives today's gradient, from that color to its neighbor on the color wheel. Two or three dots give a gradient through those colors.
  - An intensity slider, for saturation.
  - A grain slider.
- Text on the theme stays readable, per the [readable tints](arc-style-look.md#scope) rule, at every color the gradient passes through.
- Dark mode is black by default, after Vercel's neutral blacks: a black window, a near-black page card and toolbar with a thin gray edge, and white text. uw's own pages, such as the chat page, the providers page, and the command bar, use the same blacks. Chromium's own pages keep its dark gray.
- A switch in the picker, "Theme colors in dark mode", shows the theme darkened in dark mode instead of black, as Arc does.
- Suggested themes: lilac, sky, mint, butter, peach, wine, and forest as one-color themes, and three two-color gradients, such as lilac to sky, peach to butter, and mint to sky. Lilac is the default. Indigo, the old dark default, is gone.
- The "Customize Chromium" panel stays. A color picked there becomes a one-color theme, and its mode is the picker's mode. Chromium's full browser themes still replace uw's look.

Defaults, proposed by uw and accepted on 2026-10-16:

- Grain starts at none and intensity at full.
- "Theme colors in dark mode" starts off.
- A tint the user picked before themes becomes a one-color theme.
- A two- or three-color gradient runs at 160 degrees, through the dots in the order they were added.

## How a theme is saved

Chosen by uw for T94 on 2026-10-16.

- The theme's first color, at its intensity, is Chromium's own user color. Chromium's color picker, the "Customize Chromium" panel, and Chromium's sync see it, and Chromium's colors follow it.
- The colors, intensity, grain, and "Theme colors in dark mode" are four profile prefs, `uw.theme.*` in `components/look/theme_prefs.h`. They do not sync.
- When Chromium's user color is not the saved first color at the saved intensity, the color was picked somewhere else, and the theme is that one color. This covers the "Customize Chromium" panel and tints picked before themes, with no migration step.
- The window draws the more colors only when its frame color is the one the saved theme makes. Black dark mode and Chromium's own themes stay one color.

## Design

On the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Themes", drawn and signed off on 2026-10-16:

- "Theme picker: light": the picker over a lilac-to-sky window.
- "Dark mode: black": the black window and the chat page on Vercel's neutral blacks.
- "Themes: every state": one, two, and three colors, low intensity, grain, a dark color in light mode, black dark mode, and theme colors in dark mode.

The rules, with every color, are in the `uw-themes` guide. Two details the designs added: a favicon too dark for black sits on a light plate in its favorites tile, and theme colors in dark mode are each color at 14% lightness.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T93: the designs on the canvas, signed off by the user.
2. T94: the theme: up to three colors, intensity, and grain, saved per profile, the suggested themes, moving today's tint into a theme, and "Theme colors in dark mode".
3. T95: black dark mode in uw's window and pages.
4. T96: the picker: the color field and its dots, live changes, and the ways to open it.
5. T97: grain drawn over the theme.

## Acceptance checks

1. With no theme picked, light mode shows lilac. Dark mode shows a black window and a near-black toolbar and page card with a gray edge, and the chat page's background is near-black.
2. "Theme…" in the menu of empty sidebar space, View → Theme…, and the settings row each open the picker.
3. Dragging a dot changes the window as it moves. Three dots give a gradient through all three. Text keeps a 4.5:1 contrast ratio at every color of the gradient.
4. Light, Dark, and Auto change uw's window and the color scheme web pages see.
5. With "Theme colors in dark mode" on, dark mode shows the theme darkened.
6. A color picked in the "Customize Chromium" panel becomes a one-color theme.
7. A tint picked before the upgrade is the theme after it.
8. At no grain nothing is drawn. Above it, grain covers the theme and not the page card, in light and dark mode. On a theme that takes white text, grain lightens instead of darkening.
9. Two profiles keep two themes.
10. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Page mode

Status: scope agreed with the user on 2026-10-16. Not built. The design, T101, comes first.

Implementability: **7/10**. Chromium decides in one place whether a page is told light or dark, `GetPreferredColorScheme` in `chrome/browser/chrome_content_browser_client.cc`, and today it reads the [window mode](../CONTEXT.md#language). uw's pages already follow what they are told. The new parts are a profile pref, a picker row, and a patch that reads the [page mode](../CONTEXT.md#language) there and for the card's empty color.

- Page mode is Same as window, Light, or Dark. Same as window is the default, so nothing changes until the user picks.
- It covers websites and uw's own tab pages: the chat page, the providers page, and the terminal. Chromium's own pages, such as Settings, History, and the new tab page, follow the window mode, since their colors come from the window.
- Pages are only told the mode (`prefers-color-scheme`). A site with no dark look of its own stays light. Chromium's auto-dark is not used.
- The card's empty color, shown before a page paints and behind a page with no background, follows page mode: `#0A0A0A` for dark pages and white for light ones. So does the page's scrollbar.
- Open pages change as soon as the setting does.
- The control is a "Pages" row in the picker, under the window's Auto, Light, and Dark.
- It is saved per profile and does not sync. Incognito pages keep following macOS, as Chromium does.
- No per-site exceptions for now.

Stages:

1. T101: the "Pages" row drawn on the canvas's "Themes" page and signed off by the user.
2. T102: page mode built.

Acceptance checks:

1. With Same as window, pages behave as they do today.
2. In a light window with Dark pages, a website and the chat page see `prefers-color-scheme: dark`, the sidebar stays light, the toolbar takes the [page color](#page-colors), and Settings stays light.
3. In a dark window with Light pages, the reverse.
4. In a light window with Dark pages, the card is `#0A0A0A` before a page paints.
5. Changing the row changes open pages without a reload.
6. Two profiles keep two page modes. Incognito pages follow macOS.
7. `pnpm run check` and the focused suites pass. Browser tests stay headless.

## Page colors

Status: scope agreed with the user on 2026-10-16. The design, T109, and the build, T110, are in review. Nobody has used it in a real window yet.

Implementability: **5/10**. Chromium already reports each page's background color and tells the browser when it changes, and uw's color mixer already sets the toolbar's colors. The new parts are an observer of each window's shown tab, a way to give one window its own toolbar colors, and a crossfade, since rebuilding the window's colors on every frame of a fade is too slow. Chromium colors made from the toolbar's, such as the address bar's dropdown and infobars, change with it and need checking.

Agreed:

- The toolbar and the bookmarks bar under it take the [page color](../CONTEXT.md#language): the color the page paints behind its content. A red page gives a red toolbar. The sidebar, the theme around the card, and the card's gray edge in dark mode stay as they are.
- Every page counts: websites, uw's pages, and Chromium's. The terminal's black page keeps today's black toolbar.
- The toolbar follows the page whatever the window mode and page mode. A white page in a dark window gives a white toolbar. This replaces "the toolbar stays light" in [Page mode](#page-mode).
- The toolbar takes each color as soon as Chromium reports it, also while a page loads. Until a tab's page has reported one, the toolbar keeps today's color: white in light mode, `#0A0A0A` in dark mode.
- The change is a smooth fade, on a tab switch and when a page changes its color.
- Always on. No setting.

Proposed by uw and drawn for T109 on the canvas's "Themes" page, as "Page colors: a red page" and "Page colors: every state". The numbers are in the `uw-themes` guide.

- The fade takes 200 ms. The top of the card crossfades as a whole, so icons and text blend from their old color to their new one. Changed while building: the design had them switch halfway.
- The address field keeps the page color while it has focus or holds typed text. The dropdown under it keeps the window's colors. Chromium gives the focused field the dropdown's colors, which would leave white text on a white field over a red page.
- Icons and text are near-black or white, whichever reads better on the page color, as on the theme. The address field and the line under the bars are the text color at a low strength over the page color.
- A page whose color is today's toolbar color gets today's toolbar exactly, so the terminal does not change.
- In a split view, the toolbar takes the color of the page that has focus. Private windows follow the same rules.

Stages:

1. T109: page colors drawn on the canvas's "Themes" page and signed off by the user: a red page, a white page in a dark window, the terminal, and the fade. The toolbar in T101's two frames is redrawn to follow the page.
2. T110: page colors built.

Acceptance checks:

1. A page with a red background gives a red toolbar and bookmarks bar with white icons and text, in light and dark mode. The sidebar and the theme do not change.
2. The terminal tab's toolbar is `#0A0A0A`, as today.
3. Switching between a red tab and a white tab fades the toolbar from one color to the other and ends on the new one.
4. A page that changes its background, such as from a script, changes the toolbar without a reload.
5. A white page in a dark window gives a white toolbar. Text in the address field keeps a 4.5:1 contrast ratio on every page color.
6. `pnpm run check` and the focused suites pass. Browser tests stay headless.

How it is built, for T110:

- `browser/ui/views/page_colors` watches the active tab of each normal window and passes its page color on when it changes.
- `browser/ui/look/page_color` makes the toolbar's, the address field's, and the bookmarks bar's colors from it. The window takes them through the slot in Chromium's color key that web-app windows use for their own colors.
- On a change, the top of the card keeps its old look on a layer of its own and fades it out over the newly colored one.
- `PageColorsBrowserTest.*` covers checks 1 to 5 headless.

## Open questions

None.
