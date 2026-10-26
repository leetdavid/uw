// The files a message carries. Each is copied into the chat's folder first,
// where the agent's terminal opens it; the browser then reads what the model
// gets. See the agent chats plan's agreed scope and ADR 0020.
import { loadPdfjs } from "@/components/ui/file-thumbnail";
import { chatHandler, type AttachedFile, type SentFile } from "./chat-api";
import { imageData } from "./images";

export const MAX_FILES = 10;
export const MAX_IMAGES = 5;
// The images the model sees, scaled as images.ts scales them.
const IMAGE_TYPES = ["image/png", "image/jpeg", "image/webp", "image/gif"];
// pdf.js reads the text of this many pages at most.
const PDF_PAGES = 100;
// A file goes to the browser in pieces this big.
const CHUNK = 4 * 1024 * 1024;

export function isImage(type: string) {
  return IMAGE_TYPES.includes(type);
}

export function isPdf(name: string, type: string) {
  return type === "application/pdf" || /\.pdf$/i.test(name);
}

// What a file card says a file is: "PDF", its extension such as "CSV", or
// "FILE".
export function kindOf(name: string, type: string) {
  if (isPdf(name, type)) {
    return "PDF";
  }
  return /\.([a-z0-9]{1,4})$/i.exec(name)?.[1].toUpperCase() ?? "FILE";
}

// "3 KB", "1.8 MB".
export function sizeText(bytes: number) {
  if (bytes < 1024) {
    return `${bytes} bytes`;
  }
  if (bytes < 1024 * 1024) {
    return `${Math.round(bytes / 1024)} KB`;
  }
  const megabytes = bytes / (1024 * 1024);
  return megabytes < 1024
    ? `${megabytes.toFixed(1)} MB`
    : `${(megabytes / 1024).toFixed(1)} GB`;
}

// Keeps up to MAX_FILES files, at most MAX_IMAGES of them images, and says
// what was left out.
export function limit(files: File[]): { kept: File[]; note: string } {
  const kept: File[] = [];
  let images = 0;
  let imagesLeft = false;
  for (const file of files) {
    const image = isImage(file.type);
    if (kept.length < MAX_FILES && (!image || images < MAX_IMAGES)) {
      kept.push(file);
      images += image ? 1 : 0;
    } else if (image && kept.length < MAX_FILES) {
      imagesLeft = true;
    }
  }
  const left = files.length - kept.length;
  return {
    kept,
    note: !left
      ? ""
      : imagesLeft
        ? `A message holds up to ${MAX_IMAGES} images.`
        : `A message holds up to ${MAX_FILES} files. ${left} ${left === 1 ? "was" : "were"} left out.`,
  };
}

// Copies `file` into the chat's folder and answers its copy's name.
async function save(file: File): Promise<string> {
  let saved = "";
  for (let at = 0; at === 0 || at < file.size; at += CHUNK) {
    const chunk = new Uint8Array(await file.slice(at, at + CHUNK).arrayBuffer());
    // The bindings take a Uint8Array for array<uint8>, as they take a list.
    ({ saved } = await chatHandler.saveFile(file.name, chunk as unknown as number[], saved));
    if (!saved) {
      throw new Error(`${file.name} couldn't be copied.`);
    }
  }
  return saved;
}

// The text of a PDF's first pages, as pdf.js reads it, and how many pages it
// has. No pages when pdf.js can't open it.
async function pdfText(data: ArrayBuffer): Promise<{ text: string; pages: number }> {
  try {
    const pdfjs = await loadPdfjs();
    const task = pdfjs.getDocument({ data });
    const pdf = await task.promise;
    const pages: string[] = [];
    for (let number = 1; number <= Math.min(pdf.numPages, PDF_PAGES); number++) {
      const content = await (await pdf.getPage(number)).getTextContent();
      pages.push(
        content.items
          .map((item) => ("str" in item ? item.str + (item.hasEOL ? "\n" : "") : ""))
          .join(""),
      );
    }
    const read = { text: pages.join("\n\n").trim(), pages: pdf.numPages };
    await task.destroy();
    return read;
  } catch {
    return { text: "", pages: 0 };
  }
}

// What the browser needs of a file whose copy is `saved`: an image scaled for
// the model, or a PDF's text.
async function prepare(blob: Blob, name: string, type: string, saved: string): Promise<AttachedFile> {
  const file: AttachedFile = {
    saved,
    name,
    type,
    size: BigInt(blob.size),
    image: "",
    pdfText: "",
    pdfPages: 0,
  };
  if (isImage(type)) {
    file.image = await imageData(blob).catch(() => "");
  } else if (isPdf(name, type)) {
    const read = await pdfText(await blob.arrayBuffer());
    file.pdfText = read.text;
    file.pdfPages = read.pages;
  }
  return file;
}

// Copies the user's files into the chat's folder and prepares them. A file
// that can't be copied is left out and named in `failed`.
export async function attach(files: File[]): Promise<{ attached: AttachedFile[]; failed: string[] }> {
  const attached: AttachedFile[] = [];
  const failed: string[] = [];
  for (const file of files) {
    try {
      attached.push(await prepare(file, file.name, file.type, await save(file)));
    } catch {
      failed.push(file.name);
    }
  }
  return { attached, failed };
}

// The copy `saved` in the chat's folder, as the browser reads it.
export async function readCopy(saved: string, type: string): Promise<Blob | null> {
  const { data } = await chatHandler.readFile(saved);
  const bytes =
    data?.bytes !== undefined
      ? new Uint8Array(data.bytes)
      : data?.sharedMemory
        ? new Uint8Array(
            data.sharedMemory.bufferHandle.mapBuffer(0, data.sharedMemory.size).buffer,
            0,
            data.sharedMemory.size,
          )
        : null;
  return bytes ? new Blob([bytes], { type }) : null;
}

// Prepares the files the uw command attached, which the browser has already
// copied into the chat's folder.
export function prepareCopies(files: SentFile[]): Promise<AttachedFile[]> {
  return Promise.all(
    files.map(async (file) => {
      const blob =
        isImage(file.type) || isPdf(file.name, file.type)
          ? await readCopy(file.saved, file.type)
          : null;
      return blob
        ? prepare(blob, file.name, file.type, file.saved)
        : {
            saved: file.saved,
            name: file.name,
            type: file.type,
            size: file.size,
            image: "",
            pdfText: "",
            pdfPages: 0,
          };
    }),
  );
}
