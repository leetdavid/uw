/** A chat's answer drawn for the terminal: bold, headings in bold white, bullets as small dim dots, and code in the fill. */

import { Box, Text } from "ink";
import { lexer, type Token, type Tokens } from "marked";
import { createElement as h, type ReactNode } from "react";
import { COLOR } from "./palette.ts";

export function Markdown({ text }: { text: string }): ReactNode {
  return h(Box, { flexDirection: "column", gap: 1 }, blocks(lexer(text)));
}

function blocks(tokens: Token[]): ReactNode[] {
  return tokens.filter((token) => token.type !== "space").map(block);
}

function block(token: Token, key: number): ReactNode {
  switch (token.type) {
    case "heading":
      return h(Text, { key, bold: true, color: "white" }, inline(token.tokens));
    case "paragraph":
    case "text":
      return h(Text, { key, color: COLOR.text }, token.tokens ? inline(token.tokens) : token.text);
    case "list":
      return h(Box, { key, flexDirection: "column" }, (token as Tokens.List).items.map((item, at) =>
        h(Box, { key: at },
          h(Text, { color: COLOR.dim }, token.ordered ? `${Number(token.start || 1) + at}. ` : "· "),
          h(Box, { flexDirection: "column" }, blocks(item.tokens)))));
    case "code":
      return h(Box, { key, backgroundColor: COLOR.fill, paddingX: 1 }, h(Text, { color: COLOR.text }, token.text));
    case "blockquote":
      return h(Box, { key, borderStyle: "single", borderColor: COLOR.faint, borderTop: false, borderRight: false, borderBottom: false, paddingLeft: 1, flexDirection: "column" },
        blocks(token.tokens ?? []));
    case "hr":
      return h(Text, { key, color: COLOR.faint }, "───");
    default:
      return h(Text, { key, color: COLOR.text }, token.raw.trimEnd());
  }
}

function inline(tokens: Token[] = []): ReactNode[] {
  return tokens.map((token, key) => {
    switch (token.type) {
      case "strong":
        return h(Text, { key, bold: true, color: "white" }, inline(token.tokens));
      case "em":
        return h(Text, { key, italic: true }, inline(token.tokens));
      case "del":
        return h(Text, { key, strikethrough: true }, inline(token.tokens));
      case "codespan":
        return h(Text, { key, backgroundColor: COLOR.fill }, ` ${token.text} `);
      case "link":
        return h(Text, { key, color: COLOR.primary }, inline(token.tokens),
          token.text === token.href ? "" : h(Text, { color: COLOR.dim }, ` ${token.href}`));
      case "br":
        return "\n";
      case "text":
        return token.tokens ? h(Text, { key }, inline(token.tokens)) : token.text;
      default:
        return token.raw;
    }
  });
}
