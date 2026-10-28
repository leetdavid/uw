import type { ReactNode } from "react";

export function Accent({ text }: { text: ReactNode }) {
  if (typeof text !== "string") return text;
  const split = text.lastIndexOf(" ");
  if (split < 0) return <em>{text}</em>;
  return (
    <>
      {text.slice(0, split)} <em>{text.slice(split + 1)}</em>
    </>
  );
}
