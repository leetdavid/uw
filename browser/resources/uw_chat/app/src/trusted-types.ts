/// <reference types="trusted-types" />
// Radix's scroll area, under Fluid's sidebar and dropdown, writes this one
// style through innerHTML. WebUI's Trusted Types rule refuses HTML strings,
// so the default policy lets this one through and still refuses the rest.
const RADIX_SCROLL_AREA_STYLE =
  "[data-radix-scroll-area-viewport]{scrollbar-width:none;-ms-overflow-style:none;-webkit-overflow-scrolling:touch;}[data-radix-scroll-area-viewport]::-webkit-scrollbar{display:none}";

// pdf.js's worker, which the bundle puts beside the page. It is the one
// script address the page starts a worker from.
export const PDF_WORKER = new URL("/pdf.worker.js", location.origin).href;

window.trustedTypes?.createPolicy("default", {
  createHTML: (html: string) => {
    if (html !== RADIX_SCROLL_AREA_STYLE) {
      throw new TypeError("The page writes no HTML strings.");
    }
    return html;
  },
  createScriptURL: (url: string) => {
    if (url !== PDF_WORKER) {
      throw new TypeError("The page starts no other scripts.");
    }
    return url;
  },
});
