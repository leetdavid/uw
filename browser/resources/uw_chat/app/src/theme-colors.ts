// Keeps the page on the theme: chrome://theme/colors.css holds Chromium's
// colors, which the theme's first color sets, and this reloads it whenever
// the user changes the theme. uw-theme.css takes the primary color from it.
import { ColorChangeUpdater } from "//resources/cr_components/color_change_listener/colors_css_updater.js";

ColorChangeUpdater.forDocument().start();
