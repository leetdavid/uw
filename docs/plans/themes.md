# Themes

Status: scope agreed on 2026-10-16. Not built. The designs come first, T93, and the user signs them off before T94 to T97.

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

## Design

On the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Themes", drawn on 2026-10-16 and waiting for the user's sign-off:

- "Theme picker: light": the picker over a lilac-to-sky window.
- "Dark mode: black": the black window and the chat page on Vercel's neutral blacks.
- "Themes: every state": one, two, and three colors, low intensity, grain, a dark color in light mode, black dark mode, and theme colors in dark mode.

The rules, with every color, are in the `uw-themes` guide. Two details the designs added: a favicon too dark for black sits on a light plate in its favorites tile, and theme colors in dark mode are each color at 14% lightness.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T93: the designs on the canvas, signed off by the user.
2. T94: the theme: up to three colors, intensity, and grain, saved per profile, the suggested themes, and moving today's tint into a theme.
3. T95: black dark mode in uw's window and pages, and "Theme colors in dark mode".
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
8. At no grain nothing is drawn. Above it, grain covers the theme and not the page card.
9. Two profiles keep two themes.
10. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

None yet. The darkening of theme colors in dark mode is drawn as 14% lightness and waits for sign-off with the rest.
