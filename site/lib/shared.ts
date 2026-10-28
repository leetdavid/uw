import { createGetUrl } from "fumadocs-core/source";

export const helpRoute = "/help";
export const helpContentRoute = "/llms.mdx/help";
export const repo = "https://github.com/leetdavid/uw";

const getContentUrl = createGetUrl(helpContentRoute);

export function getPageMarkdownUrl(page: { slugs: string[] }) {
  const segments = [...page.slugs, "content.md"];
  return { segments, url: getContentUrl(segments) };
}
