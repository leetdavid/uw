import {
  DocsBody,
  DocsDescription,
  DocsPage,
  DocsTitle,
  MarkdownCopyButton,
  PageLastUpdate,
  ViewOptionsPopover,
} from "fumadocs-ui/layouts/docs/page";
import { createRelativeLink } from "fumadocs-ui/mdx";
import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { Accent } from "@/components/accent";
import { Feedback } from "@/components/feedback";
import { getMDXComponents } from "@/components/mdx";
import { getPageMarkdownUrl, repo } from "@/lib/shared";
import { groupOf, source } from "@/lib/source";

export default async function Page(props: PageProps<"/help/[...slug]">) {
  const { slug } = await props.params;
  const page = source.getPage(slug);
  if (!page) notFound();

  const MDX = page.data.body;
  const markdownUrl = getPageMarkdownUrl(page).url;
  const actions = (
    <div className="help-actions">
      <MarkdownCopyButton markdownUrl={markdownUrl}>
        Copy as Markdown
      </MarkdownCopyButton>
      <ViewOptionsPopover
        markdownUrl={markdownUrl}
        githubUrl={`${repo}/blob/main/site/content/help/${page.path}`}
      >
        Open in
      </ViewOptionsPopover>
    </div>
  );

  return (
    <DocsPage
      toc={page.data.toc}
      tableOfContent={{ footer: actions }}
      breadcrumb={{ enabled: false }}
    >
      <p className="help-group">{groupOf(page.url)}</p>
      <DocsTitle>
        <Accent text={page.data.title} />
      </DocsTitle>
      <DocsDescription>{page.data.description}</DocsDescription>
      <div className="help-actions-narrow">{actions}</div>
      <DocsBody>
        <MDX
          components={getMDXComponents({ a: createRelativeLink(source, page) })}
        />
      </DocsBody>
      <Feedback page={page.url} />
      <div className="help-end">
        {page.data.lastModified && (
          <PageLastUpdate date={page.data.lastModified} />
        )}
        <span>
          Something wrong here?{" "}
          <a href={`${repo}/issues/new`}>Report an issue</a>
        </span>
      </div>
    </DocsPage>
  );
}

export const instant = false;

export function generateStaticParams() {
  return source.generateParams();
}

export async function generateMetadata(
  props: PageProps<"/help/[...slug]">,
): Promise<Metadata> {
  const { slug } = await props.params;
  const page = source.getPage(slug);
  if (!page) notFound();

  return {
    title: `${page.data.title} · uw Help`,
    description: page.data.description,
  };
}
