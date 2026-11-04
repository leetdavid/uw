# The theme's light or dark comes from its colors

Replaces [ADR 0017](0017-the-themes-light-or-dark-rides-in-chromiums-color-key.md).

On 2026-11-04 the user asked for one Auto, Light, and Dark toggle that changes only how websites look, and a theme picker that sets uw's look with no dark mode of its own, black included. In a grilling session they agreed that the [theme](../CONTEXT.md#language) covers everything uw and Chromium draw, their own pages included, and that [website mode](../CONTEXT.md#language) covers only websites and extensions' pages. We decided that a theme whose text is white is dark, and Chromium's color key takes that light or dark, so Chromium's menus, dialogs, and pages follow the theme with no work per surface. Website mode is Chromium's own light or dark setting, one per profile, and reaches only the pages it covers, through `GetPreferredColorScheme` and the card's empty color.

ADR 0017 put page mode in the key because page mode then covered Chromium's surfaces, and pushing it onto each one needed a hook per surface. Website mode covers only web content, which Chromium tells its light or dark in one place, so that reason is gone and the key no longer carries two modes. The sidebar's extra mixers go with it.

Considered options: keeping ADR 0017's split with page mode in the key (rejected: Chromium's pages and menus would follow websites, which the user no longer wants), and a light or dark setting of uw's own for website mode (rejected: Chromium's setting already exists and its controls in Settings and the "Customize Chromium" panel would disagree with it).

Consequences: Chromium's light or dark setting no longer changes Chromium's own look in uw, so its label in Settings becomes "Websites". A page in a tab must be told the theme's light or dark when it is uw's or Chromium's own, and website mode when it is a website, so `GetPreferredColorScheme` decides by address, as T102 did.
