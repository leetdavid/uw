import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { TestContext } from "node:test";
import { execute, layoutAt, requireCleanWorktrees, requireOwnedRoot } from "../scripts/chromium.ts";
import type { Runner } from "../scripts/chromium.ts";

function fixture(t: TestContext) {
  const temp = mkdtempSync(join(tmpdir(), "uw-git-test-"));
  t.after(() => rmSync(temp, { recursive: true, force: true }));
  const layout = layoutAt(join(temp, "checkout"));
  requireOwnedRoot(layout, true);
  const env = {
    ...process.env, GIT_CONFIG_NOSYSTEM: "1", GIT_CONFIG_GLOBAL: "/dev/null",
    GIT_CONFIG_COUNT: "1", GIT_CONFIG_KEY_0: "diff.ignoreSubmodules", GIT_CONFIG_VALUE_0: "dirty",
    GIT_AUTHOR_NAME: "uw test", GIT_AUTHOR_EMAIL: "test@example.invalid",
    GIT_COMMITTER_NAME: "uw test", GIT_COMMITTER_EMAIL: "test@example.invalid",
  };
  const run: Runner = (command, options) => execute(command, { ...options, env });
  const git = (path: string, args: string[], input?: string): string => {
    const result = spawnSync("git", ["-C", path, ...args], { input, env, encoding: "utf8" });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim();
  };
  const recordTree = (path: string): void => {
    // Synthetic Git objects in the fixture; never commits the user's repository.
    const tree = git(path, ["write-tree"]);
    const revision = git(path, ["commit-tree", tree], "fixture\n");
    git(path, ["update-ref", "HEAD", revision]);
  };
  const makeRepo = (path: string): void => {
    mkdirSync(path, { recursive: true }); git(path, ["init", "--quiet"]);
    writeFileSync(join(path, "tracked.txt"), "original\n");
    git(path, ["add", "tracked.txt"]); recordTree(path);
  };
  makeRepo(layout.source);
  const dependency = join(layout.source, "v8"); makeRepo(dependency);
  writeFileSync(join(layout.source, ".gitmodules"), '[submodule "v8"]\n\tpath = v8\n\turl = https://example.invalid/v8.git\n');
  git(layout.source, ["add", ".gitmodules"]);
  const stageDependency = (): void => {
    const revision = git(dependency, ["rev-parse", "HEAD"]);
    git(layout.source, ["update-index", "--add", "--cacheinfo", `160000,${revision},v8`]);
  };
  stageDependency(); recordTree(layout.source);
  const advanceDependency = (): void => {
    writeFileSync(join(dependency, "tracked.txt"), "updated dependency revision\n");
    git(dependency, ["add", "tracked.txt"]); recordTree(dependency);
  };
  return { layout, dependency, run, git, makeRepo, stageDependency, advanceDependency };
}

test("dependency revision drift can resume syncing", (t) => {
  const f = fixture(t); f.advanceDependency();
  assert.match(f.git(f.layout.source, ["status", "--porcelain"]), /v8/);
  requireCleanWorktrees(f.layout, f.run);
});

test("dirty dependencies are detected despite upstream's ignore setting", (t) => {
  const f = fixture(t);
  writeFileSync(join(f.dependency, "tracked.txt"), "user work\n");
  assert.equal(f.git(f.layout.source, ["status", "--porcelain"]), "");
  assert.throws(() => requireCleanWorktrees(f.layout, f.run), /v8 has local changes/);
  assert.equal(readFileSync(join(f.dependency, "tracked.txt"), "utf8"), "user work\n");
});

test("untracked dependency files are preserved", (t) => {
  const f = fixture(t);
  writeFileSync(join(f.dependency, "notes.txt"), "untracked work\n");
  assert.throws(() => requireCleanWorktrees(f.layout, f.run), /v8 has local changes/);
  assert.equal(readFileSync(join(f.dependency, "notes.txt"), "utf8"), "untracked work\n");
});

test("staged gitlinks are not mistaken for interrupted sync", (t) => {
  const f = fixture(t); f.advanceDependency(); f.stageDependency();
  assert.throws(() => requireCleanWorktrees(f.layout, f.run), /src has local changes/);
});

test("extra roots cover dependencies without gitlinks", (t) => {
  const f = fixture(t); const legacy = join(f.layout.source, "legacy"); f.makeRepo(legacy);
  writeFileSync(join(f.layout.source, ".git/info/exclude"), "legacy/\n");
  writeFileSync(join(legacy, "tracked.txt"), "more user work\n");
  assert.throws(() => requireCleanWorktrees(f.layout, f.run, [legacy]), /legacy has local changes/);
});

test("upstream dependency paths cannot escape the owned checkout", (t) => {
  const f = fixture(t);
  assert.throws(() => requireCleanWorktrees(f.layout, f.run, ["/tmp"]), /escapes the checkout/);
});
