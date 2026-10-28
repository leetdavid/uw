import { isMarkdownPreferred, rewritePath } from "fumadocs-core/negotiation";
import { type NextRequest, NextResponse } from "next/server";
import { helpContentRoute, helpRoute } from "@/lib/shared";

const { rewrite: rewritePage } = rewritePath(
  `${helpRoute}{/*path}`,
  `${helpContentRoute}{/*path}/content.md`,
);
const { rewrite: rewriteSuffix } = rewritePath(
  `${helpRoute}{/*path}.md`,
  `${helpContentRoute}{/*path}/content.md`,
);

export default function proxy(request: NextRequest) {
  const suffix = rewriteSuffix(request.nextUrl.pathname);
  if (suffix) return NextResponse.rewrite(new URL(suffix, request.nextUrl));

  if (isMarkdownPreferred(request)) {
    const page = rewritePage(request.nextUrl.pathname);
    if (page) {
      return NextResponse.rewrite(new URL(page, request.nextUrl), {
        headers: { Vary: "Accept" },
      });
    }
  }

  return NextResponse.next();
}

export const config = {
  matcher: "/help/:path+",
};
