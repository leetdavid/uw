# The theme's light or dark rides in Chromium's color key

On 2026-10-25 the user asked for the sidebar's theme and Chromium's look to be fully apart: "the cleaner implementation would be to completely separate the sidebar theming from the chromium theming", so that a light sidebar can sit beside dark pages, dark Settings, and dark menus. Chromium works out every color of a window from one color key, whose light or dark was the theme's. We decided that the key's light or dark is the [page mode](../plans/themes.md#page-mode), and that a field of uw's own in the key carries the theme's light or dark, read only by uw's colors for the sidebar and its menus.

Chromium keeps colors by key, so two windows with the same page mode and different themes need different keys: the key is the only place the theme's mode can live. Every Chromium menu, bubble, dialog, and page then follows page mode with no work per surface. The cost is a patch to `ui/color/color_provider_key.h`, which rebuilds nearly all of Chromium once and must be carried at each Chromium update.

Considered options: keeping the window on the theme's mode and pushing page mode onto each Chromium surface (rejected: every bubble, menu, dialog, and panel in the window needs its own hook, and each one missed stays in the wrong mode, as Settings and the extensions menu did after T191).
