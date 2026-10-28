import { notFound } from "next/navigation";
import { getPageMarkdownUrl } from "@/lib/shared";
import { helpLlms, source } from "@/lib/source";

export async function GET(
  _req: Request,
  { params }: RouteContext<"/llms.mdx/help/[...slug]">,
) {
  const { slug } = await params;
  const page = source.getPage(slug.slice(0, -1));
  if (!page) notFound();

  return new Response(await helpLlms.page(page), {
    headers: { "Content-Type": "text/markdown" },
  });
}

export function generateStaticParams() {
  return source
    .getPages()
    .map((page) => ({ slug: getPageMarkdownUrl(page).segments }));
}
