# Themes

Status: scope agreed on 2026-10-16, and the designs, T93, signed off the same day. T94 to T97, the saved theme, black dark mode, the picker, and grain, were built that day. All four were checked in a real window and done on 2026-10-22. Changed on 2026-11-04: the theme loses its light and dark modes and website mode takes the place of page mode, per [One look](#one-look).

Implementability: **5/10** using the [roadmap convention](roadmap.md#implementability-estimates). Gradients, black dark mode, and the saved theme reuse the tint code that exists. The picker is the risk: a color field with dots to drag is a custom control.

The look of the window is in the [Arc-style look plan](arc-style-look.md). This plan replaces its single tint color with a [theme](../CONTEXT.md#language), in the way Arc's theme picker works.

## Agreed scope

- A profile has one theme, shared by all its windows. Each profile can look different. Changed on 2026-10-21: each [workspace](workspaces.md) has its own theme, T131.
- The picker opens over the window, beside the sidebar, from "Theme…" in the menu of empty sidebar space and from View → Theme… in the menu bar. The window changes as the user picks. The "Window tint" section of uw's settings page becomes one row that opens the picker.
- The picker has, from the top:
  - Light, Dark, and Auto. Auto follows macOS. Since T201 this sets the theme alone, and [page mode](#page-mode) sets everything else. Replaced on 2026-11-04 by the Websites row, per [One look](#one-look).
  - Suggested themes.
  - A color field with up to three dots to drag. One dot gives today's gradient, from that color to its neighbor on the color wheel. Two or three dots give a gradient through those colors.
  - An intensity slider, for saturation.
  - A grain slider.
- Text on the theme stays readable, per the [readable tints](arc-style-look.md#scope) rule, at every color the gradient passes through.
- Dark mode is black by default, after Vercel's neutral blacks: a black window, a near-black page card and toolbar with a thin gray edge, and white text. uw's own pages, such as the chat page, the providers page, and the command bar, use the same blacks. Chromium's own pages keep its dark gray. Replaced on 2026-11-04: there is no dark mode, and Black is a suggested theme, per [One look](#one-look).
- Chromium's lines, such as the line above "Manage Extensions" in the extensions menu, are neutral gray in light mode, as uw's own lines are, and not a light tone of the theme's color. Since T202, after the user saw a cyan line there on 2026-10-25.
- A switch in the picker, "Theme colors in dark mode", shows the theme darkened in dark mode instead of black, as Arc does. Removed on 2026-11-04.
- Suggested themes: lilac, sky, mint, butter, peach, wine, and forest as one-color themes, and three two-color gradients, such as lilac to sky, peach to butter, and mint to sky. Lilac is the default. Indigo, the old dark default, is gone. Changed on 2026-11-04: lilac to sky is the default and comes first, and Black is added, per [One look](#one-look).
- The "Customize Chromium" panel stays. A color picked there becomes a one-color theme, and its mode is the picker's mode. Changed on 2026-11-04: its mode is website mode. Chromium's full browser themes still replace uw's look.
- Agreed on 2026-10-24: the chat app's primary color, on the chat page, the AI page, and the watch card, is the theme's color. It is Chromium's primary color, which the theme's first color sets, read from `chrome://theme/colors.css`, and it changes at once with the theme. It covers primary buttons and the scheduled chats' accents, which the frames draw in the primary color. Blue stays for links and notices elsewhere.

Defaults, proposed by uw and accepted on 2026-10-16:

- Grain starts at none and intensity at full.
- "Theme colors in dark mode" starts off. Removed on 2026-11-04 with the switch.
- A tint the user picked before themes becomes a one-color theme.
- A two- or three-color gradient runs at 160 degrees, through the dots in the order they were added.

## How a theme is saved

Chosen by uw for T94 on 2026-10-16.

- The theme's first color, at its intensity and, since T338, its brightness, is Chromium's own user color. Chromium's color picker, the "Customize Chromium" panel, and Chromium's sync see it, and Chromium's colors follow it.
- The colors, intensity, grain, and "Theme colors in dark mode" are four profile prefs, `uw.theme.*` in `components/look/theme_prefs.h`. They do not sync. Since T338 the switch is gone, `uw.theme.brightness` is added, and the pref the switch left behind is no longer read.
- When Chromium's user color is not the saved first color at the saved intensity, the color was picked somewhere else, and the theme is that one color. This covers the "Customize Chromium" panel and tints picked before themes, with no migration step.
- The window draws the more colors only when its frame color is the one the saved theme makes. Chromium's own themes stay one color.
- Since T131, on 2026-10-24, each workspace keeps its own theme; see [How workspace windows work](workspaces.md#how-workspace-windows-work). The profile's settings above hold the latest pick in any of its workspaces, which the "Customize Chromium" panel and the profile's private windows show.

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

Status: scope agreed with the user on 2026-10-16. The design, T101, was signed off on 2026-10-24, and T102 built it the same day. The user approved the build that day and tried it in uw: "it looks good". Replaced on 2026-11-04 by website mode, per [One look](#one-look).

Implementability: **7/10**. Chromium decides in one place whether a page is told light or dark, `GetPreferredColorScheme` in `chrome/browser/chrome_content_browser_client.cc`, and today it reads the [window mode](../CONTEXT.md#language). uw's pages already follow what they are told. The new parts are a part of the theme, a picker row, and a patch that reads the [page mode](../CONTEXT.md#language) there and for the card's empty color.

- Page mode is Theme, Light, or Dark. Theme, the default, is the theme's own light or dark, so nothing changes until the user picks. Its tooltip and VoiceOver label say "Same as the theme". It read "Window" until T201.
- It covers everything outside the sidebar: websites, extensions' pages wherever they show (their popups, side panels, and tabs), uw's tab pages, Chromium's pages such as Settings, History, and the new tab page, Chromium's menus, bubbles, and dialogs, the side panel, the find bar, infobars, the toolbar until its page reports a color, and uw's panels over the page: the command bar, the address bar's suggestion list, and the "Watch this page" card.
- The theme covers the sidebar, the edge around the page card, and the menus and cards that open from the sidebar: the task menu, Organize, the new button's menu, the rename and new workspace cards, this picker, and macOS's tab and pin menus.
- Dark pages use [black dark mode](#agreed-scope): neutral Chromium colors and uw's blacks, whatever "Theme colors in dark mode" says, since that switch is the theme's. Light pages use Chromium's light colors tinted by the theme, as a light window did.
- Agreed step by step: T191 on 2026-10-24, "the extensions are in light mode when the theme is light, but pages are dark. they should follow the pages mode"; T198, of the command bar, "i am saying that it should be in dark mode, but it is currently in light mode for some reason"; T199, "the address bar is not fixed yet"; and T201 on 2026-10-25, in a grilling session, after "the dark mode of chromium needs to be separated out from the sidebar's theming" and "the cleaner implementation would be to completely separate the sidebar theming from the chromium theming".
- Pages are only told the mode (`prefers-color-scheme`). A site with no dark look of its own stays light. Chromium's auto-dark is not used.
- The card's empty color, shown before a page paints and behind a page with no background, follows page mode: `#0A0A0A` for dark pages and white for light ones.
- The page's scrollbar follows the toolbar, as in Chromium, and the toolbar takes the [page color](#page-colors). A site with no dark look of its own keeps a light scrollbar. Agreed on 2026-10-24; this replaced "the scrollbar follows page mode".
- Open pages change as soon as the setting does.
- The control is a "Pages" row in the picker, under the theme's Auto, Light, and Dark.
- It belongs to the theme, so each [workspace](workspaces.md) has its own, as its window mode is, and a new workspace starts on the page mode of the one it was made from. Agreed on 2026-10-24; it was per profile. It does not sync. In private windows, pages and Chromium's menus and bubbles follow macOS, as Chromium's private pages do, and the sidebar shows the profile's latest theme.
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
6. Two workspaces keep two page modes. Incognito pages follow macOS.
7. `pnpm run check` and the focused suites pass. Browser tests stay headless.

How it is built, for T102:

- The theme keeps `page_mode`, in each workspace's theme and in `uw.theme.page_mode` for windows without a workspace.
- `PageModeIsDark` in `browser/workspaces/workspace_theme.h` decides for each page from its window's theme and its address. A page outside a tab, such as an extension's popup, finds its window through the bubble or panel that holds it. `uw-scheme-alias` asks it in `GetPreferredColorScheme`, and `uw-look` asks `EmptyPageColor` in `ContentsWebView` for the card, also after each navigation.
- A change redraws the workspace's windows, which makes Chromium tell each open page again.
- `WorkspacesBrowserTest.PagesTakeThePageModeAndChromiumsPagesTheWindow`, `TwoWorkspacesKeepTwoPageModes`, `OpenPagesTakeANewPageModeAtOnce`, and `TheCardBehindAPageTakesThePageMode` cover checks 2 to 6, `AnExtensionsPopupTakesThePageMode` an extension's popup, and `ThemePickerBrowserTest.ThePagesRowSetsThePageMode` the row.

### The theme apart from Chromium

Status: agreed with the user on 2026-10-25 in a grilling session. The designs, T200, were pre-approved: "the designs are pre-approved, just start the implementation right after". T201 built it the same day. Nobody has used it in a real window yet. Replaced on 2026-11-04 by [One look](#one-look) and [ADR 0030](../adr/0030-the-themes-light-or-dark-comes-from-its-colors.md): Chromium's menus, dialogs, and pages follow the theme again.

Implementability: **5/10**. Chromium works out every color of a window from one color key, which held the theme's light or dark. Page mode becomes the key's light or dark, and the theme's key rides along in it for the sidebar's colors, per [ADR 0017](../adr/0017-the-themes-light-or-dark-rides-in-chromiums-color-key.md). Every Chromium surface then follows page mode by itself. The risks are general Chromium colors that a view in the sidebar uses, uw's sidebar menus and cards, which must ask for the theme's mode, and macOS's own menus, which take the window's look.

Stages:

1. T200: the two "Page mode" frames redrawn: the row's first choice reads "Theme", a light sidebar beside dark Settings and dark Chromium menus, and a dark sidebar beside light pages.
2. T201: the split built. It replaces the per-surface hooks of T191, T198, and T199.

Acceptance checks:

1. With a light theme and Dark pages: websites, an extension's popup, Settings, the ⋮ menu, the extensions menu, the command bar, and the suggestion list are dark; the sidebar, the edge around the card, and the task menu stay light.
2. With a dark theme and Light pages, the reverse, with the sidebar black or in its theme colors as the theme says.
3. With Pages on Theme, every surface looks as it did before T201.
4. Two workspaces keep their own theme and page mode. In a private window, pages and Chromium's menus follow macOS.
5. `pnpm run check` and the focused suites pass. Browser tests stay headless.

How it is built, for T201:

- `ApplyThemeOfWindow` (`browser/workspaces/workspace_theme.h`) gives a window's key the page mode, neutral black for dark pages, and, when that differs from the theme, the mixers of `browser/ui/look/theme_colors.h` in the key's slot for extra mixers. They copy the theme key's colors for Chromium's header, frame, tab, task, and window button colors, picked by name, and for uw's cards and the edge around the page card. The page color's mixers come along in the same slot.
- Chromium tells each page its light or dark from the window's key, so websites, Chromium's pages, uw's pages, and every bubble, menu, and dialog follow page mode by themselves. The per-surface hooks of T191, T198, and T199 are gone.
- `ShowInThemeMode` keeps the theme picker and the sidebar's cards in the theme's mode, and `ShowOpenMenuInThemeMode` the views menus the sidebar opens, also when the theme changes while they are open. macOS's tab and pin menus take the window's look, whose background is the theme's.
- `WorkspacesBrowserTest.EverythingButTheSidebarTakesThePageMode` covers checks 1 and 2 and `TheSidebarKeepsTheThemeUnderAnyPageMode` compares the sidebar's pixels between Pages on Theme and on Dark, and on Light for a dark theme. `ThemePickerBrowserTest.ThePagesRowSetsThePageMode` checks that the picker keeps the theme's light under dark pages.

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
- The address field keeps the page color while it has focus or holds typed text. The dropdown under it keeps the window's colors, or takes the [page mode](#page-mode) when one is set (T199). Chromium gives the focused field the dropdown's colors, which would leave white text on a white field over a red page.
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

## Glass

Status: scope agreed with the user on 2026-10-24 in a grilling session, after they asked for "an option to have glass in the theming now, like apple's liquid glass" that affects "the pinned tabs, the sidebar's transparency, etc." The design, T174, was signed off the same day. After trying the first build, the user split the one setting in two the same day: "macos decided to have just the glass effect on UI elements of the window, so let's make the transparency of the window a separate control, and glass only affects the UI elements". Being built in T175.

Implementability: **4/10**. Chromium already draws a Liquid Glass frame on macOS 26 and later (`GlassFrameService`, and the `NSGlassEffectView` in `browser_native_widget_mac.mm`). With vertical tabs it covers only the sidebar, so uw widens it to the whole window and paints the theme and the page color see-through over it. Glass on the window's elements is drawing only. uw's menus, cards, the command bar, and the picker are each their own small window, so each kind needs its own glass view. Real glass on each pin tile, T176, is the hardest part. Glass needs a real window on a real screen, so headless tests check the settings and the rules, and the look is checked by eye.

Transparency, the window:

- A "Transparency" slider in the theme picker, under Grain. It belongs to the theme, so each workspace has its own. At the left end the window is solid, as today; that is the default.
- Moving it right lets the desktop show through the sidebar, the strip around the page card, the toolbar, and the bookmarks bar, blurred by macOS's glass. The theme covers the glass from 100% at the left end down to 20% at the right end. The page stays solid, starts below the toolbar, and does not scroll under it; Blink cannot lay a desktop page out under the toolbar.
- Text keeps a 4.5:1 contrast ratio over any desktop while the theme covers the glass at 60% or more, or 75% on a theme with white text. Clearer than that, the user picks clear over readable, as macOS allows. This is the one exception to the [readable tints](arc-style-look.md#scope) rule. A small tick on the track marks that point; its tooltip and VoiceOver label say "Clearer than this, text may be hard to read on some desktops."
- The toolbar and the bookmarks bar keep the page color over the glass, as much as the theme covers it, but never so little that the toolbar's text stops reading over a white or a black desktop: 52% for a white page, 56% for the terminal's `#0A0A0A`, and 86% for a red `#C62828`.
- Every visible uw window is see-through, as on macOS. A window that is not active is frosted more, which macOS does by itself to clear glass. A switch on uw's settings page, under the Theme row, turns that off for all of uw, since it saves power rather than changing a workspace's look: off, only the front window is see-through. Its label is to become "Transparency in windows behind the front one", since "Glass" now names the elements. The Theme row's second line is "The window's colors, grain, glass, and light or dark mode".
- The window is solid in full screen, with Energy Saver on, with Reduce transparency on in macOS, and under a Chrome Web Store theme. Chromium has each of these rules but Reduce transparency, which uw adds. Pop-up windows are never see-through. Private windows follow the profile's latest pick, as they show that pick's theme. While it is paused, the picker says why under the sliders.

Glass, the window's elements:

- A "Glass" slider under Transparency, also part of the theme. At the left end the elements are flat, as today; that is the default.
- Moving it right turns pin tiles, the active tab, and the Organize and new buttons at the sidebar's foot into lit glass: a white that fades down the element, a lit top edge, and a faint ring, from frosted, keeping all of the white, to clear, keeping a seventh of it, like the Liquid Glass slider in macOS's settings. The active tab keeps enough white to stand out.
- It works whatever the Transparency. Over a solid sidebar, the elements show the sidebar's color through them, as macOS's glass controls show what is behind them in the window. Agreed on 2026-10-24: no holes cut through the sidebar under the pin tiles.
- Agreed on 2026-10-24, "add the glass effect to any other leftover interfaces": the workspace switcher's current workspace and the Scratchpad's Tidy and Clear under the pointer are glass pills, and the activity card above the foot is a glass card, its color at 85% under the lit edge. The tab and pin menus are macOS's own, which macOS draws as Liquid Glass already.
- uw's menus and cards, the command bar, the theme picker, and the "Watch this page" card are frosted glass while Glass is off its left end, as macOS's menus are. The picker frosts as the slider moves. They use macOS's heavier regular glass under their own color at about 85%, so their text always keeps 4.5:1. The cards include the sidebar's, such as the rename card.
- Agreed on 2026-10-24, "go as your recommendation": the new button's menu, the new workspace card's profile list, and the ⋮ menu with its submenus are frosted the same way, in Chromium's menu color at 85%. Then, "i want it anyway, to unify the visual language": the address bar's suggestion list too, which shows while the command bar is off. Its glass leaves out the address it opens around, so typed text stays sharp. The find bar, hover cards, and Chromium's other menus and popups stay solid.
- Reduce transparency in macOS makes the elements flat too.
- Real glass on each pin tile, which bends what is behind it, is T176, prototyped first. If a tile's glass trails its favicon for a visible frame during a drag or the swipe, or glass on glass looks muddy, the user sees the prototype, T176 closes as tried, and the drawn tiles stay.

Before macOS 26 the picker has neither slider. On Windows it has Glass alone, per the [Windows plan](windows.md#agreed): the tiles and buttons turn to glass, and menus and cards stay solid.

The numbers are in the `uw-themes` guide, section "Glass". The frames are on the canvas's "Themes" page: "Glass: light", "Glass: dark", and "Glass: every state".

How it is built, for T175:

- The theme keeps `transparency` and `glass` from 0 to 100, as `uw.theme.transparency` and `uw.theme.glass`, and so does each workspace's theme. `components/look/theme` turns them into how much the theme covers the glass, the place of the tick, the toolbar's least cover, and how much white a glass element keeps.
- `browser/ui/glass` holds uw's rules: a window asks for Liquid Glass behind it when its theme's transparency is above 0, the Mac runs macOS 26 or later, and Reduce transparency is off. The settings switch is `uw.glass.windows_behind` in local state.
- Chromium's `GlassFrameService` picks the windows that show it. The `uw-look` patch starts it, and the glass parts of the window's layout, on macOS 26 whatever the `GlassFrame` feature says, and swaps its `glass_frame.enabled` switch and its limit of one window for uw's rules. Its Energy Saver, full screen, and web store theme rules stay.
- The service hears of a new transparency through Chromium's theme settings, so uw saves a workspace's theme before them.
- The `tab-tree` patch spans the `NSGlassEffectView` behind the window across the whole window, in Apple's clear style, with no tint of its own.
- `BrowserFrameViewMac` paints the theme at its cover around the page card. The toolbar and the bookmarks bar paint the page color at the toolbar's cover. Over glass the top container paints nothing behind them, and the toolbar's top corners have no fill of their own.
- `PaintGlassTabFill` draws pin tiles and the active tab as lit glass, and `browser/ui/glass/glass_background` the foot's buttons, the switcher, Tidy and Clear, and the activity card. Tiles have no drop shadow, since a tab cannot paint outside itself.
- A card's bubble gets macOS's regular glass behind it, an `NSGlassEffectView` inside its shadow, and its color at 85%: `FrostOverGlass` for uw's own cards, and `FrostPageBubble` for the bubbles that hold a web page, whose page draws its own see-through color while the window's glass is on.
- `RoundedOmniboxResultsFrame::UpdateFrost`, in `uw-look`, frosts the suggestion list each time it shows. The list's page draws no color of its own; the frame behind it paints the list's color. `SetFrostedGlass` cuts the address out of the glass with a mask.
- `FrostOpenMenu` frosts a menu just after it opens. A hook that `uw-look` adds to Chromium's `MenuHost` frosts each of its submenus before it shows.

Stages:

1. T174: the glass frames, signed off by the user.
2. T175: both settings built, with drawn glass on the pin tiles.
3. T176: real glass on each pin tile, prototyped first.

Acceptance checks:

1. With both sliders at the left end, the window looks as it does today.
2. With Transparency moved right, in the front window on macOS 26 or later, the desktop shows through the sidebar, the toolbar, and around the page, blurred. The page stays solid.
3. At the right end of Transparency, the toolbar on a white page covers the glass at 52% and on a `#C62828` page at 86%, and its address keeps 4.5:1 over a white and a black desktop.
4. With the theme covering the glass at 60%, or 75% on a theme with white text, text on the sidebar and the toolbar keeps 4.5:1 over a white and a black desktop, in light and dark mode.
5. With Glass moved right and Transparency at the left end, pin tiles, the active tab, and the foot's buttons are lit glass over a solid sidebar, clearer the further right.
6. Two uw windows side by side are both see-through. The one not active is more frosted. With the switch for windows behind off, it is solid.
7. Energy Saver, Reduce transparency, full screen, and a Chrome Web Store theme each make the window solid. Reduce transparency also makes the elements flat. For the first two, the picker says why.
8. Two workspaces keep their own Transparency and Glass.
9. With Glass off its left end, a task's menu, the new button's menu, the profile list, the ⋮ menu and its submenus, the address bar's suggestion list, the command bar, the picker, and the "Watch this page" card show what is behind them frosted, and their text keeps 4.5:1. The switcher's current workspace, Tidy and Clear under the pointer, and the activity card are lit glass.
10. `pnpm run check` and the focused suites pass. Browser tests stay headless.

## One look

Status: agreed with the user on 2026-11-04 in a grilling session, after "I want one theme toggle (auto, light, dark), and this ONLY controls the page's content's theme", "the color picker and thing will faithfully change uw's theme (mainly the sidebar), without a "dark" mode. you should be able to just pick black too", "Theme colors in dark mode doesn't seem to do anything, so let's remove that", and "uw's own interfaces should use the uw theme". The design, T337, was signed off the same day: "this looks good". T338 and T339 wait for the user's go-ahead.

Implementability: **6/10**. Most of the work removes code: the theme's mode, the switch, black dark mode, the Pages row, and the sidebar mixers of [ADR 0017](../adr/0017-the-themes-light-or-dark-rides-in-chromiums-color-key.md). Chromium has the OKLCH math, in `ui/gfx/color_conversions.h`, and OKLCH gradients, in `SkGradient::Interpolation`. The risks are Chromium's colors made from a dark theme color that is not black, telling uw's and Chromium's pages in tabs the theme's light or dark while websites get website mode, and extensions' popups finding their window.

Agreed:

- The theme has no light or dark of its own. A theme whose text is white is dark, by the [readable tints](arc-style-look.md#scope) rule that already picks the text. With two or three colors, one text color covers the whole gradient, so the theme has one answer.
- The theme's light or dark covers the sidebar, the edge around the page card, the sidebar's menus and cards, the picker, uw's pages such as the chat, providers, scheduled, and settings pages and the terminal, the command bar, the "Watch this page" card, and Chromium's menus, bubbles, dialogs, and pages such as Settings, History, and the new tab page.
- [Website mode](../CONTEXT.md#language) is Auto, Light, or Dark, and covers websites, extensions' pages and popups, and the card before a page paints: `#0A0A0A` or white. Auto, the default, follows macOS; its tooltip and VoiceOver label say "Same as macOS". Pages are only told the mode, so a site with no dark look of its own stays light.
- Website mode is one per profile, kept in Chromium's own light or dark setting, `browser_color_scheme`. Private windows follow it. In Settings → Appearance, "Mode" reads "Websites", and VoiceOver reads the "Customize Chromium" panel's mode buttons as "Websites". Open pages change at once.
- The picker, from the top: the Websites row, the color field, the suggested themes, Intensity, Brightness, Grain, Transparency, and Glass. The picker takes the theme's light or dark, and changes as a drag crosses from one to the other.
- Brightness, from 0 to 100, is one slider for the whole theme, so a gradient cannot run from white to black and leave no readable text. 100, the default, is the colors as picked, and 0 is black. It scales each color's OKLCH lightness. Its track runs from black to the theme's first color. The field stays at full brightness, and the dots show each color as drawn.
- OKLCH for the field, Brightness, Intensity, which scales each color's OKLCH chroma, and the gradient between dots. The gradient turns through the hues between two dots, so sky to butter passes through green. Colors are still saved as sRGB.
- Suggested themes, in order: lilac to sky, lilac, sky, mint, butter, peach, wine, forest, black, peach to butter, and mint to sky, as 20px circles. Wine is a rose at Brightness 43, forest a pale green at 37, and black any color at 0. Lilac to sky is the default for everyone, whatever macOS says.
- A new workspace takes the next unused suggested theme at the Brightness of the workspace it was made from.
- Text stays near-black or white at 4.5:1. A mid-tone where neither reads is still made a little lighter or darker.
- The toolbar keeps the [page color](#page-colors). Proposed by uw: before a page reports one, it is white or `#0A0A0A` as website mode says, like the card behind it.
- On upgrade, each workspace keeps its colors at full Brightness. Website mode starts from Chromium's setting, or from `uw.theme.page_mode` where the Pages row said Light or Dark. The theme's mode, page mode, and "Theme colors in dark mode" leave the saved themes, the profile prefs, and the `uw` command's `theme.mode`, `theme.page-mode`, and `theme.colors-in-dark-mode`.

How it is to be built is in [ADR 0030](../adr/0030-the-themes-light-or-dark-comes-from-its-colors.md): Chromium's color key takes the theme's light or dark, and website mode reaches only the pages it covers, through `GetPreferredColorScheme` and the card's empty color.

Design: the frames "One look: lilac to sky, dark websites", "One look: black theme, light websites", and "One look: every state" on the canvas's "Themes" page.

Stages:

1. T337: the design, signed off on 2026-11-04.
2. T338: the theme: light or dark from its colors, Brightness, the new suggestions, OKLCH, the removals, and the upgrade, per ADR 0030.
3. T339: website mode: Chromium's setting, the Websites row, `GetPreferredColorScheme` and the card's empty color, the strings in Settings and the panel, the `uw` command, and the Help page.

How T338 is built:

- `components/look/tint` turns colors to and from OKLCH with Chromium's `ui/gfx/color_conversions.h`. A color too colorful for sRGB keeps its lightness and hue and loses chroma until it fits. The tint's second color, the readable nudge, Intensity, and Brightness all work in OKLCH there and in `components/look/theme`.
- `IsDarkTheme` is the theme's light or dark: its text is white. `ApplyThemeOfWindow` puts it in the window's color key, so Chromium's own colors, uw's pages, and the sidebar's menus and cards follow it. Chromium's own setting for the profile gets it too, for windows without a workspace and the "Customize Chromium" panel.
- A first color with almost no chroma, such as black, gives Chromium's neutral colors (`IsNeutral`). The same rule in `ThemeService::GetColorProviderKey`, in `uw-look`, replaces black dark mode.
- The window's gradient blends in OKLCH (`SkGradient::Interpolation::ColorSpace::kOKLCHGamutMap`).
- The picker's page asks the browser for the color under a point of the field and where a color sits on it, so the field's math lives only in `components/look/theme`. The page draws the field with CSS `oklch()` at the same lightness and chroma.
- Until T339, the Pages row keeps working through ADR 0017's mixers, and dark pages keep Chromium's neutral colors.

Acceptance checks:

1. With lilac to sky and Websites on Dark, a website, an extension's popup, and the card before a page paints are dark. The sidebar, its menus, the picker, the chat page, Settings, the ⋮ menu, and the command bar are light.
2. With black and Websites on Light, the reverse, with a `#000000` sidebar.
3. Mint at Brightness 80 takes near-black text and light menus; at 60 and below, white text and dark menus. The picker changes during the drag.
4. At Brightness 0 any colors give a black sidebar, and at 100 the colors as picked. Text keeps 4.5:1 at every color of a two- or three-color gradient, which blends in OKLCH.
5. The picker's row, Chromium's Settings, and the "Customize Chromium" panel set one website mode per profile and always agree. Auto follows macOS. Private windows follow it.
6. After the upgrade, each workspace keeps its colors, and a profile whose Pages row said Dark keeps dark websites.
7. A new profile starts on lilac to sky with Websites on Auto. A new workspace keeps the Brightness of the one it was made from.
8. `pnpm run check` and the focused suites pass. Browser tests stay headless.

## Open questions

None.
