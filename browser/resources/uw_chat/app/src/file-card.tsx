// A file in the composer or above a sent message, other than an image the
// model sees: a page shape with a PDF's first page or the file's extension,
// then its name over its kind and size. See the canvas guide
// uw-attached-files.
import { useEffect, useState } from "react";
import { renderPdfFirstPage } from "@/components/ui/file-thumbnail";
import { Tooltip } from "@/components/ui/tooltip";
import { isPdf, kindOf, readCopy, sizeText } from "./files";

// What a card's amber note means, under the pointer.
const MISSED: Record<string, string> = {
  "read in part":
    "The model read the first 100,000 characters. The agent has the whole file and can read on in its terminal.",
  "first 100 pages read":
    "The model read the text of the first 100 pages. The agent has the whole file and can read on in its terminal.",
  "no text": "The model reads this PDF's text, and it has none. The agent has the file.",
};

export function FileCard({
  name,
  type,
  size,
  pdf,
  missed = "",
  onOpen,
}: {
  name: string;
  type: string;
  size: number;
  // A PDF whose first page the card shows: the file, or the name of its copy
  // in the chat's folder.
  pdf?: Blob | string;
  // What the model missed of the file, as SentFile says it.
  missed?: string;
  onOpen?: () => void;
}) {
  const [page, setPage] = useState<string | null>(null);
  useEffect(() => {
    if (!pdf || !isPdf(name, type)) {
      return;
    }
    let gone = false;
    (async () => {
      const blob =
        typeof pdf === "string" ? await readCopy(pdf, "application/pdf").catch(() => null) : pdf;
      const drawn = blob ? await renderPdfFirstPage(blob, 32).catch(() => null) : null;
      if (!gone) {
        setPage(drawn);
      }
    })();
    return () => {
      gone = true;
    };
  }, [pdf, name, type]);
  const kind = kindOf(name, type);
  const card = (
    <div
      className="file-card group/tile relative flex h-14 max-w-[228px] shrink-0 cursor-default items-center gap-2.5 rounded-xl bg-[var(--selected)] pr-3.5 pl-2 ring-1 ring-border"
      data-name={name}
      onClick={onOpen}
      role={onOpen ? "button" : undefined}
    >
      <span className="relative grid h-10 w-8 shrink-0 place-items-end justify-center overflow-hidden rounded-[5px] bg-accent pb-1 text-[8.5px] font-semibold tracking-wide text-muted-foreground">
        {page ? (
          <img src={page} alt="" className="absolute inset-0 h-full w-full bg-white object-cover object-top" />
        ) : (
          kind
        )}
      </span>
      <span className="flex min-w-0 flex-col">
        <b className="truncate text-[13px] font-medium text-foreground">{name}</b>
        <span className="file-kind truncate text-xs text-muted-foreground">
          {kind} · {sizeText(size)}
          {missed && <span className="file-missed text-warning"> · {missed}</span>}
        </span>
      </span>
    </div>
  );
  return MISSED[missed] ? (
    <Tooltip content={MISSED[missed]} contentClassName="max-w-[250px]">
      {card}
    </Tooltip>
  ) : (
    card
  );
}
