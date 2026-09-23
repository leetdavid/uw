/** Expose product source to GN without copying or owning the source files. */
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, readlinkSync, rmdirSync, symlinkSync, unlinkSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const MARKER = ".uw-source-links";
const MARKER_TEXT = "uw browser managed source links\n";
const DIRECTORIES = ["browser", "components", "resources"] as const;

export class SourceLinks {
  readonly root: string;
  readonly repository: string;

  constructor(source: string, repository: string) {
    this.root = join(source, "uw");
    this.repository = repository;
  }

  check(): void {
    if (!existsSync(this.root)) return;
    if (!lstatSync(this.root).isDirectory() || lstatSync(this.root).isSymbolicLink() ||
        !existsSync(join(this.root, MARKER)) ||
        readFileSync(join(this.root, MARKER), "utf8") !== MARKER_TEXT) {
      throw new Error(`${this.root} is not a managed source directory. Preserve it before preparing uw.`);
    }
    for (const name of readdirSync(this.root)) {
      if (name === MARKER) continue;
      const path = join(this.root, name);
      if (!DIRECTORIES.some((directory) => directory === name) || !lstatSync(path).isSymbolicLink() ||
          resolve(this.root, readlinkSync(path)) !== resolve(this.repository, name)) {
        throw new Error(`${path} differs from the managed source link. Preserve it before preparing uw.`);
      }
    }
  }

  prepare(): void {
    this.check();
    for (const name of DIRECTORIES) {
      if (!existsSync(join(this.repository, name))) throw new Error(`Missing product source: ${name}`);
    }
    if (!existsSync(this.root)) {
      mkdirSync(this.root);
      writeFileSync(join(this.root, MARKER), MARKER_TEXT);
    }
    const present = new Set(readdirSync(this.root));
    for (const name of DIRECTORIES) {
      if (!present.has(name)) symlinkSync(resolve(this.repository, name), join(this.root, name), "dir");
    }
  }

  verify(): void {
    this.check();
    if (!DIRECTORIES.every((name) => existsSync(join(this.root, name)))) {
      throw new Error("Product source links are missing. Run pnpm run prepare:chromium.");
    }
  }

  remove(): void {
    this.check();
    if (!existsSync(this.root)) return;
    // Unlink only known links. Never follow them or recursively delete source.
    for (const name of readdirSync(this.root)) unlinkSync(join(this.root, name));
    rmdirSync(this.root);
  }
}
