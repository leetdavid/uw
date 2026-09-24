import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, unlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { TestContext } from "node:test";
import { SourceLinks } from "../scripts/source-links.ts";

function fixture(t: TestContext) {
  const root = mkdtempSync(join(tmpdir(), "uw-sources-test-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const source = join(root, "src");
  const repository = join(root, "product");
  mkdirSync(source); mkdirSync(repository);
  for (const name of ["browser", "components"]) mkdirSync(join(repository, name));
  const file = join(repository, "browser", "feature.cc");
  writeFileSync(file, "product source\n");
  return { source, repository, file, links: new SourceLinks(source, repository) };
}

test("source links are repeatable and reflect product edits without copying", (t) => {
  const f = fixture(t);
  f.links.prepare(); f.links.prepare(); f.links.verify();
  writeFileSync(f.file, "edited product source\n");
  assert.equal(readFileSync(join(f.source, "uw/browser/feature.cc"), "utf8"), "edited product source\n");
  f.links.remove();
  assert.equal(existsSync(join(f.source, "uw")), false);
  assert.equal(readFileSync(f.file, "utf8"), "edited product source\n");
});

test("an unrelated source directory is preserved", (t) => {
  const f = fixture(t);
  mkdirSync(join(f.source, "uw"));
  writeFileSync(join(f.source, "uw/notes.txt"), "user work");
  assert.throws(() => f.links.prepare(), /not a managed/);
  assert.throws(() => f.links.remove(), /not a managed/);
  assert.equal(readFileSync(join(f.source, "uw/notes.txt"), "utf8"), "user work");
});

test("unexpected files and changed link targets stop preparation and removal", (t) => {
  const f = fixture(t);
  f.links.prepare();
  writeFileSync(join(f.source, "uw/notes.txt"), "user work");
  assert.throws(() => f.links.remove(), /differs from the managed/);
  unlinkSync(join(f.source, "uw/notes.txt"));
  unlinkSync(join(f.source, "uw/browser"));
  symlinkSync(f.source, join(f.source, "uw/browser"));
  assert.throws(() => f.links.prepare(), /differs from the managed/);
  assert.throws(() => f.links.remove(), /differs from the managed/);
  assert.equal(readFileSync(f.file, "utf8"), "product source\n");
});

test("missing links from an interrupted preparation can be recreated", (t) => {
  const f = fixture(t);
  f.links.prepare();
  unlinkSync(join(f.source, "uw/components"));
  assert.throws(() => f.links.verify(), /links are missing/);
  f.links.prepare(); f.links.verify();
});
