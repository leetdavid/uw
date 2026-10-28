import type * as PageTree from "fumadocs-core/page-tree";
import { llms, loader } from "fumadocs-core/source";
import { metaSchema, pageSchema } from "fumadocs-core/source/schema";
import { defineDocs } from "fumadocs-mdx/macro";
import type { ReactNode } from "react";
import { helpRoute } from "./shared";

const help = defineDocs({
  dir: "content/help",
  docs: {
    schema: pageSchema,
    lastModified: true,
    mdxOptions: async (environment) => {
      const { applyMdxPreset } = await import("fumadocs-mdx/config");
      return applyMdxPreset({
        rehypeCodeOptions: {
          themes: { light: "github-dark-default", dark: "github-dark-default" },
        },
      })(environment);
    },
    postprocess: {
      includeProcessedMarkdown: true,
    },
  },
  meta: {
    schema: metaSchema,
  },
});

export const source = loader({
  baseUrl: helpRoute,
  source: help.toFumadocsSource(),
});

export interface Group {
  name: ReactNode;
  pages: PageTree.Item[];
}

export function groups(): Group[] {
  const list: Group[] = [];
  for (const node of source.getPageTree().children) {
    if (node.type === "separator") list.push({ name: node.name, pages: [] });
    if (node.type === "page") list.at(-1)?.pages.push(node);
  }
  return list;
}

export function groupOf(url: string) {
  return groups().find((group) => group.pages.some((page) => page.url === url))
    ?.name;
}

export const helpLlms = llms(source, {
  renderPage: async (page) => `# ${page.data.title} (${page.url})

${page.data.description}

${await page.data.getText("processed")}`,
});
