import { replaceSource } from "fumadocs-core/mdx-plugins/stringifier";

type File = Parameters<typeof replaceSource>[0];
type Node = Parameters<typeof replaceSource>[1];

// A page's Markdown copy and its search records show a Help figure as its
// caption.
export function remarkFigureCaptions() {
  return (tree: Node, file: File) => {
    const visit = (node: Node) => {
      if (node.type === "mdxJsxFlowElement" && node.name === "Figure") {
        const caption = node.attributes.find(
          (attribute) =>
            attribute.type === "mdxJsxAttribute" &&
            attribute.name === "caption",
        )?.value;
        if (typeof caption === "string") {
          replaceSource(file, node, `*Figure: ${caption}*`);
          node.data = {
            ...node.data,
            structuredData: {
              contents: [{ heading: undefined, content: caption }],
            },
          };
        }
        return;
      }
      if ("children" in node) {
        for (const child of node.children) visit(child);
      }
    };
    visit(tree);
  };
}
