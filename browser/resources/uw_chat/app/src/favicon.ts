// The browser's icon for the page at `url`.
export function favicon(url: string): string {
  return `chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${encodeURIComponent(url)}`;
}
