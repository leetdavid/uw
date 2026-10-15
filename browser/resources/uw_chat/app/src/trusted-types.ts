/// <reference types="trusted-types" />
// Radix's scroll area, under Fluid's sidebar and dropdown, writes this one
// style through innerHTML. WebUI's Trusted Types rule refuses HTML strings,
// so the default policy lets this one through and still refuses the rest.
const RADIX_SCROLL_AREA_STYLE =
  "[data-radix-scroll-area-viewport]{scrollbar-width:none;-ms-overflow-style:none;-webkit-overflow-scrolling:touch;}[data-radix-scroll-area-viewport]::-webkit-scrollbar{display:none}";

window.trustedTypes?.createPolicy("default", {
  createHTML: (html: string) => {
    if (html !== RADIX_SCROLL_AREA_STYLE) {
      throw new TypeError("The page writes no HTML strings.");
    }
    return html;
  },
});
