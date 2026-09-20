import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { TestContext } from "node:test";
import { execute, layoutAt, requireCleanWorktrees } from "../scripts/chromium.ts";
import type { Runner } from "../scripts/chromium.ts";
import { PatchSet } from "../scripts/patches.ts";

function fixture(t: TestContext) {
  const root = mkdtempSync(join(tmpdir(), "uw-patches-test-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const layout = layoutAt(root);
  const directory = join(root, "patches");
  const state = join(root, "applied.json");
  mkdirSync(layout.source); mkdirSync(directory);
  const env = {
    ...process.env, GIT_CONFIG_NOSYSTEM: "1", GIT_CONFIG_GLOBAL: "/dev/null",
    GIT_AUTHOR_NAME: "uw test", GIT_AUTHOR_EMAIL: "test@example.invalid",
    GIT_COMMITTER_NAME: "uw test", GIT_COMMITTER_EMAIL: "test@example.invalid",
  };
  const run: Runner = (command, options) => execute(command, { ...options, env: { ...options?.env, ...env,
    ...(options?.env?.GIT_INDEX_FILE ? { GIT_INDEX_FILE: options.env.GIT_INDEX_FILE } : {}),
  } });
  const git = (args: string[], input?: string) => run(["git", "-C", layout.source, ...args], { input });
  git(["init", "--quiet"]);
  const file = join(layout.source, "tracked.txt");
  writeFileSync(file, "before\noriginal\n\n");
  writeFileSync(join(layout.source, "other.txt"), "other original\n");
  git(["add", "."]);
  // Synthetic fixture objects, never commits the user's repository.
  git(["update-ref", "HEAD", git(["commit-tree", git(["write-tree"])], "fixture\n")]);
  const patch = (from: string, to: string) =>
    `diff --git a/tracked.txt b/tracked.txt\n--- a/tracked.txt\n+++ b/tracked.txt\n@@ -1,3 +1,3 @@\n before\n-${from}\n+${to}\n \n`;
  writeFileSync(join(directory, "first.patch"), patch("original", "first"));
  writeFileSync(join(directory, "second.patch"), patch("first", "second"));
  writeFileSync(join(directory, "series"), "first.patch\nsecond.patch\n");
  const patches = new PatchSet(layout.source, directory, state, run);
  return { layout, directory, state, file, git, run, patch, patches };
}

test("overlapping patches follow series order, preserve the index, and repeat without rewriting files", (t) => {
  const f = fixture(t);
  const head = f.git(["rev-parse", "HEAD"]);
  f.patches.apply();
  assert.equal(readFileSync(f.file, "utf8"), "before\nsecond\n\n");
  const modified = statSync(f.file).mtimeMs;
  f.patches.apply(); f.patches.verify();
  assert.equal(statSync(f.file).mtimeMs, modified);
  assert.equal(f.git(["diff", "--cached"]), "");
  assert.equal(f.git(["rev-parse", "HEAD"]), head);
  assert.throws(() => requireCleanWorktrees(f.layout, f.run), /local changes/);
  requireCleanWorktrees(f.layout, f.run, [], () => { f.patches.check(); });
  f.patches.unapply();
  requireCleanWorktrees(f.layout, f.run);
  assert.equal(existsSync(f.state), false);
});

test("changed and removed patches replace the old recorded result", (t) => {
  const f = fixture(t);
  f.patches.apply();
  writeFileSync(join(f.directory, "series"), "first.patch\n");
  writeFileSync(join(f.directory, "first.patch"), f.patch("original", "updated"));
  assert.throws(() => f.patches.verify(), /out of date/);
  f.patches.apply();
  assert.equal(readFileSync(f.file, "utf8"), "before\nupdated\n\n");
  f.patches.verify();
});

test("a conflict in a later patch leaves the previously prepared tree intact", (t) => {
  const f = fixture(t);
  f.patches.apply();
  const recorded = readFileSync(f.state, "utf8");
  writeFileSync(join(f.directory, "second.patch"), f.patch("not present", "bad"));
  assert.throws(() => f.patches.apply(), /second.patch could not be prepared.*\n.*patch failed: tracked.txt/s);
  assert.equal(readFileSync(f.file, "utf8"), "before\nsecond\n\n");
  assert.equal(readFileSync(f.state, "utf8"), recorded);
  assert.equal(f.git(["diff", "--cached"]), "");
  // Removing old patches before sync does not depend on today's series files.
  rmSync(f.directory, { recursive: true });
  f.patches.unapply();
  assert.equal(readFileSync(f.file, "utf8"), "before\noriginal\n\n");
});

for (const kind of ["patched", "unrelated", "staged", "untracked"] as const) {
  test(`prepare and sync preserve ${kind} user edits`, (t) => {
    const f = fixture(t);
    f.patches.apply();
    const target = kind === "patched" ? f.file : join(f.layout.source, kind === "untracked" ? "notes.txt" : "other.txt");
    writeFileSync(target, "user work\n");
    if (kind === "staged") f.git(["add", "other.txt"]);
    assert.throws(() => f.patches.apply(), /local changes/);
    assert.throws(() => f.patches.unapply(), /local changes/);
    assert.throws(() => requireCleanWorktrees(f.layout, f.run, [], () => { f.patches.check(); }), /local changes/);
    assert.equal(readFileSync(target, "utf8"), "user work\n");
  });
}

test("preparation resumes after interruption between recording and applying", (t) => {
  const f = fixture(t);
  f.patches.apply();
  const recorded = readFileSync(f.state, "utf8");
  f.patches.unapply();
  writeFileSync(f.state, JSON.stringify({ ...JSON.parse(recorded), phase: "applying" }));
  f.patches.apply(); f.patches.verify();
  assert.equal(readFileSync(f.file, "utf8"), "before\nsecond\n\n");
});

for (const reverse of [false, true]) {
  test(`preparation recovers after ${reverse ? "reversing" : "applying"} only part of a multi-file patch`, (t) => {
    const f = fixture(t);
    writeFileSync(join(f.directory, "second.patch"),
      "diff --git a/other.txt b/other.txt\n--- a/other.txt\n+++ b/other.txt\n@@ -1 +1 @@\n-other original\n+other patched\n");
    if (reverse) f.patches.apply();
    const interrupted = new PatchSet(f.layout.source, f.directory, f.state, (command, options) => {
      if (command.includes("apply") && !command.includes("--cached") && !command.includes("--check")) {
        const firstFile = options!.input!.split(/(?=^diff --git )/m)[0]!;
        f.run(command, { ...options, input: firstFile });
        throw new Error("interrupted after first file");
      }
      return f.run(command, options);
    });
    assert.throws(() => reverse ? interrupted.unapply() : interrupted.apply(), /interrupted after first file/);
    assert.notEqual(f.git(["diff"]), "");
    f.patches.apply(); f.patches.verify();
    assert.equal(readFileSync(f.file, "utf8"), "before\nfirst\n\n");
    assert.equal(readFileSync(join(f.layout.source, "other.txt"), "utf8"), "other patched\n");
  });

  test(`preparation recovers a removed file during ${reverse ? "reversal" : "application"}`, (t) => {
    const f = fixture(t);
    if (reverse) f.patches.apply();
    const interrupted = new PatchSet(f.layout.source, f.directory, f.state, (command, options) => {
      if (command.includes("apply") && !command.includes("--cached") && !command.includes("--check")) {
        rmSync(f.file);
        throw new Error("interrupted between removing and replacing a file");
      }
      return f.run(command, options);
    });
    assert.throws(() => reverse ? interrupted.unapply() : interrupted.apply(), /interrupted between/);
    assert.equal(existsSync(f.file), false);
    f.patches.apply(); f.patches.verify();
    assert.equal(readFileSync(f.file, "utf8"), "before\nsecond\n\n");
  });
}

test("completed preparations reject later per-file reverts as user edits", (t) => {
  const f = fixture(t);
  writeFileSync(join(f.directory, "second.patch"),
    "diff --git a/other.txt b/other.txt\n--- a/other.txt\n+++ b/other.txt\n@@ -1 +1 @@\n-other original\n+other patched\n");
  f.patches.apply();
  writeFileSync(f.file, "before\noriginal\n\n");
  assert.throws(() => f.patches.apply(), /local changes/);
  assert.throws(() => f.patches.unapply(), /local changes/);
});

test("completed preparations preserve a full user revert", (t) => {
  const f = fixture(t);
  f.patches.apply();
  writeFileSync(f.file, "before\noriginal\n\n");
  assert.throws(() => f.patches.apply(), /local changes/);
  assert.throws(() => f.patches.unapply(), /local changes/);
  assert.equal(f.git(["diff"]), "");
});

test("all patch commands inherit the supplied environment, including temporary-index commands", (t) => {
  const f = fixture(t);
  let temporaryIndex = false;
  const patches = new PatchSet(f.layout.source, f.directory, f.state, (command, options) => {
    assert.equal(options?.env?.UW_TEST_ENV, "configured");
    if (options?.env?.GIT_INDEX_FILE) temporaryIndex = true;
    return f.run(command, options);
  }, { ...process.env, UW_TEST_ENV: "configured" });
  patches.apply(); patches.verify(); patches.unapply();
  assert.ok(temporaryIndex);
});

test("unrecorded changes and invalid series fail without modifying the source", (t) => {
  const f = fixture(t);
  f.patches.apply(); rmSync(f.state);
  assert.throws(() => f.patches.apply(), /local changes/);
  writeFileSync(f.file, "before\noriginal\n\n");
  for (const series of ["", "../first.patch\n", "first.patch\nfirst.patch\n"]) {
    writeFileSync(join(f.directory, "series"), series);
    assert.throws(() => f.patches.apply(), /unique patch filenames/);
    assert.equal(f.git(["diff"]), "");
  }
});

test("unsupported new files fail before source mutation", (t) => {
  const f = fixture(t);
  writeFileSync(join(f.directory, "second.patch"),
    "diff --git a/new.txt b/new.txt\nnew file mode 100644\n--- /dev/null\n+++ b/new.txt\n@@ -0,0 +1 @@\n+new\n");
  assert.throws(() => f.patches.apply(), /tracked Chromium files/);
  assert.equal(f.git(["status", "--porcelain"]), "");
  assert.equal(existsSync(f.state), false);
});
