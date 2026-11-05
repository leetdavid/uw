# Only the sidebar takes the theme

Replaces [ADR 0030](0030-the-themes-light-or-dark-comes-from-its-colors.md) where it puts the theme's light or dark in Chromium's color key.

On 2026-11-05, after trying T339, the user said: "uw's own pages should respect page theme", "ONLY the sidebar theme stays, everything else needs to follow the page theme". Asked about what opens from the sidebar: "Unless we style it to feel like the theme, it needs to follow the page theme." We decided that the [theme](../CONTEXT.md#language) colors the sidebar and the strip around the page card, and the [mode](../CONTEXT.md#language) sets everything else: websites, uw's and Chromium's own pages, menus, cards, panels, and the toolbar.

A theme whose text is white is still dark, as ADR 0030 decided, but that light or dark now covers only the sidebar. The window's color key takes the mode, as ADR 0017 had it for page mode. The theme's own key rides in the key's slot for extra mixers, which copy the sidebar's colors from it by Chromium's color families: the frame, the vertical tab strip, tabs, tab groups, the sidebar's top buttons, and a site's picture-in-picture bar. Families that open over the page, such as Tab Search and tab hover cards, and uw's menu cards stay with the mode, so no panel mixes the two.

Considered options: keeping ADR 0030's key (rejected: uw's pages, the toolbar, and menus followed the theme, which the user did not want), and setting each sidebar view's colors by hand (rejected: Chromium draws the tabs from dozens of colors that would each need a hook).

Consequences: the mixers come back, with their cost of one extra set of colors per theme and mode pair. A new Chromium color family drawn in the sidebar must be added to the list, or it follows the mode. Websites and extensions' pages, private ones included, read the mode from Chromium's setting in `GetPreferredColorScheme`; uw's and Chromium's own pages get it from the color key.
