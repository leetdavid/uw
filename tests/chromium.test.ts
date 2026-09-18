import assert from "node:assert/strict";
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import { test } from "node:test";
import type { TestContext } from "node:test";
import {
  BuildError, CHROMIUM_URL, Chromium, DEPOT_TOOLS_URL, GIB, MARKER, REPO,
  SYNC_STAMP, layoutAt, loadPins, requireOwnedRoot,
} from "../scripts/chromium.ts";
import type { CommandOptions, Pins, Runtime } from "../scripts/chromium.ts";

const pins: Pins = {
  version: "1.2.3.4", chromium_revision: "a".repeat(40),
  depot_tools_revision: "b".repeat(40), macos_sdk_min: "26.5",
};

function temporary(t: TestContext): string {
  const path = mkdtempSync(join(tmpdir(), "uw-tooling-test-"));
  t.after(() => rmSync(path, { recursive: true, force: true }));
  return path;
}

class Fixture {
  readonly layout;
  readonly browser: Chromium;
  readonly calls: { command: readonly string[]; options: CommandOptions }[] = [];
  readonly messages: string[] = [];
  readonly heads = new Map<string, string>();
  readonly origins = new Map<string, string>();
  readonly dirty = new Set<string>();
  readonly runtime: Runtime;
  free = 250 * GIB;
  xcodeReady = true;
  curlReady = true;
  sdk = "26.5";
  filesystem = "apfs";
  bootstrapSucceeds = true;
  bootstrapped = false;
  failSync = false;
  extraRoots: string[] = [];
  omitSourceRoot = false;
  dom = '<html><body data-uw-smoke="passed"></body></html>';

  constructor(t: TestContext) {
    this.layout = layoutAt(join(temporary(t), "checkout"));
    this.runtime = {
      platform: "darwin", arch: "arm64", nodeVersion: "24.13.0", memoryBytes: 16 * GIB,
      env: {}, freeBytes: () => this.free, log: (line) => this.messages.push(line),
      run: (command, options = {}) => this.execute(command, options),
    };
    this.browser = new Chromium(this.layout, pins, this.runtime);
  }

  makeRepo(path: string, origin: string, revision: string): void {
    mkdirSync(join(path, ".git"), { recursive: true });
    this.origins.set(path, origin);
    this.heads.set(path, revision);
  }

  execute(command: readonly string[], options: CommandOptions): string {
    this.calls.push({ command, options });
    const [program, operation] = command;
    assert.ok(program);
    if (options.inherit) {
      assert.equal(options.env?.DEPOT_TOOLS_UPDATE, "0");
      assert.equal(options.env?.DEPOT_TOOLS_DIR, this.layout.tools);
      assert.equal(options.env?.DEPOT_TOOLS_BOOTSTRAP_PYTHON3, "1");
    }
    if ((program === "git" || program === "curl") && operation === "--version") {
      if (program === "curl" && !this.curlReady) throw new BuildError("curl unavailable");
      return `${program} fixture version`;
    }
    if (program === "xcodebuild") {
      if (!this.xcodeReady) throw new BuildError("Xcode unavailable");
      return operation === "-version" ? "Xcode 26.5" : "";
    }
    if (program === "xcrun") return this.sdk;
    if (program === "df") return "Filesystem blocks used available capacity Mounted on\n/dev/test 1 1 1 1% /test";
    if (program === "diskutil") return "upstream plist fixture";
    if (program === "plutil") { assert.equal(options.input, "upstream plist fixture"); return this.filesystem; }
    if (program === "git" && operation === "-C") {
      const path = command[2];
      assert.ok(path);
      const args = command.slice(3);
      if (args.join(" ") === "rev-parse --show-toplevel") return path;
      if (args.join(" ") === "remote get-url origin") return this.origins.get(path) ?? "";
      if (args.join(" ") === "rev-parse HEAD") return this.heads.get(path) ?? "";
      if (args[0] === "status") return this.dirty.has(path) ? " M changed.cc" : "";
      if (args[0] === "diff" || args[0] === "ls-files") return "";
    }
    if (program === "git" && operation === "clone") {
      this.makeRepo(this.layout.tools, DEPOT_TOOLS_URL, "c".repeat(40)); return "";
    }
    if (program === "git" && operation === "fetch") return "";
    if (program === "git" && operation === "checkout") {
      assert.ok(options.cwd); this.heads.set(options.cwd, command.at(-1)!); this.bootstrapped = false; return "";
    }
    if (basename(program) === "ensure_bootstrap") {
      this.bootstrapped = this.bootstrapSucceeds;
      if (this.bootstrapped) {
        const directory = join(this.layout.tools, "runtime/bin");
        mkdirSync(directory, { recursive: true });
        writeFileSync(join(directory, "python3"), "upstream runtime fixture");
        chmodSync(join(directory, "python3"), 0o755);
        writeFileSync(join(this.layout.tools, "python3_bin_reldir.txt"), "runtime/bin");
      }
      return "";
    }
    if (basename(program) === "gclient") {
      assert.ok(this.bootstrapped, "upstream tools must be bootstrapped first");
      if (operation === "config") {
        assert.ok(command.includes("--unmanaged")); assert.ok(command.includes(CHROMIUM_URL));
        writeFileSync(join(this.layout.root, ".gclient"), "upstream-generated configuration fixture\n"); return "";
      }
      if (operation === "recurse") {
        const roots = this.omitSourceRoot ? this.extraRoots : [this.layout.source, ...this.extraRoots];
        return roots.map((root) => `UW_CHECKOUT_ROOT=${JSON.stringify(root)}`).join("\n");
      }
      if (operation === "sync") {
        assert.ok(command.includes(`src@${pins.chromium_revision}`));
        if (this.failSync) throw new BuildError("sync interrupted");
        this.makeRepo(this.layout.source, CHROMIUM_URL, pins.chromium_revision);
        mkdirSync(join(this.layout.source, "chrome"), { recursive: true });
        writeFileSync(join(this.layout.source, "chrome/VERSION"), "MAJOR=1\nMINOR=2\nBUILD=3\nPATCH=4\n");
        writeFileSync(join(this.layout.root, ".gclient_entries"), "opaque upstream entries fixture");
        return "";
      }
    }
    if (basename(program) === "gn") return "";
    if (basename(program) === "autoninja") {
      mkdirSync(dirname(this.layout.binary), { recursive: true }); writeFileSync(this.layout.binary, "browser fixture"); return "";
    }
    if (program === this.layout.binary) return operation === "--version" ? `Chromium ${pins.version}` : this.dom;
    throw new Error(`Unexpected command: ${JSON.stringify(command)}`);
  }
}

test("repository pins are valid and invalid pins fail before commands", (t) => {
  assert.match(loadPins().chromium_revision, /^[a-f0-9]{40}$/);
  const path = join(temporary(t), "pins.json");
  for (const invalid of [
    { chromium_revision: "main" }, { depot_tools_revision: "latest" },
    { version: 123 }, { macos_sdk_min: "latest" }, { extra: "value" },
  ]) {
    writeFileSync(path, JSON.stringify({ ...pins, ...invalid }));
    assert.throws(() => loadPins(path), BuildError);
  }
});

test("unrelated directories are preserved", (t) => {
  const f = new Fixture(t);
  mkdirSync(f.layout.root); writeFileSync(join(f.layout.root, "notes.txt"), "keep this");
  assert.throws(() => requireOwnedRoot(f.layout, true), /dedicated empty/);
  assert.equal(readFileSync(join(f.layout.root, "notes.txt"), "utf8"), "keep this");
  assert.equal(existsSync(join(f.layout.root, MARKER)), false);
});

test("managed roots can be reused without replacing files", (t) => {
  const f = new Fixture(t);
  requireOwnedRoot(f.layout, true); writeFileSync(join(f.layout.root, "notes.txt"), "keep this");
  requireOwnedRoot(f.layout, true);
  assert.equal(readFileSync(join(f.layout.root, "notes.txt"), "utf8"), "keep this");
});

test("host checks are read-only and report all blockers", (t) => {
  const f = new Fixture(t);
  assert.equal(f.browser.doctor(), true);
  f.xcodeReady = false; f.free = 13 * GIB; f.sdk = "15.0"; f.filesystem = "exfat"; f.curlReady = false;
  assert.equal(f.browser.doctor(), false);
  for (const label of ["Xcode", "Free disk", "macOS SDK", "Filesystem", "curl"]) {
    assert.ok(f.messages.some((line) => line.startsWith(`FAIL ${label}:`)));
  }
  assert.equal(existsSync(f.layout.root), false);
  assert.throws(() => f.browser.sync(2), /Preflight failed/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("repeated sync reuses checkouts and keeps both revisions pinned", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  const config = readFileSync(join(f.layout.root, ".gclient"), "utf8");
  f.browser.sync(2);
  assert.equal(readFileSync(join(f.layout.root, ".gclient"), "utf8"), config);
  assert.equal(f.calls.filter(({ command }) => command[0] === "git" && command[1] === "clone").length, 1);
  assert.equal(f.heads.get(f.layout.tools), pins.depot_tools_revision);
  assert.equal(f.heads.get(f.layout.source), pins.chromium_revision);
  f.browser.verifyCheckout();
});

test("sync refuses edited source before mutation", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); f.dirty.add(f.layout.source); f.calls.length = 0;
  assert.throws(() => f.browser.sync(2), /local changes/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("sync preserves changed upstream configuration", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); const config = join(f.layout.root, ".gclient");
  writeFileSync(config, "custom configuration"); f.calls.length = 0;
  assert.throws(() => f.browser.sync(2), /configuration is unrecorded or changed/);
  assert.equal(readFileSync(config, "utf8"), "custom configuration");
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("changed pins and interrupted sync cannot build against a stale stamp", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  const changed = new Chromium(f.layout, { ...pins, chromium_revision: "d".repeat(40) }, f.runtime);
  assert.throws(() => changed.build(2), /Sync has not completed/);
  f.failSync = true;
  assert.throws(() => f.browser.sync(2), /sync interrupted/);
  assert.throws(() => f.browser.build(2), /Sync has not completed/);
});

test("incomplete upstream bootstrap stops before source download", (t) => {
  const f = new Fixture(t);
  f.bootstrapSucceeds = false;
  assert.throws(() => f.browser.sync(2));
  assert.equal(existsSync(join(f.layout.root, SYNC_STAMP)), false);
  assert.equal(f.calls.some(({ command }) => basename(command[0]!) === "gclient"), false);
});

test("unexpected source origin is rejected before compilation", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); f.origins.set(f.layout.source, "https://example.invalid/other"); f.calls.length = 0;
  assert.throws(() => f.browser.build(2), /origin must be/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("non-gitlink dependencies reported by upstream are checked before syncing", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  const legacy = join(f.layout.source, "legacy");
  f.makeRepo(legacy, "https://example.invalid/legacy", "e".repeat(40));
  f.extraRoots = [legacy]; f.dirty.add(legacy); f.calls.length = 0;
  assert.throws(() => f.browser.sync(2), /legacy has local changes/);
  assert.equal(f.calls.some(({ command }) => basename(command[0]!) === "gclient" && command[1] === "sync"), false);
});

test("previously synced dependencies stay protected after disappearing from the current graph", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  const legacy = join(f.layout.source, "legacy");
  f.makeRepo(legacy, "https://example.invalid/legacy", "e".repeat(40));
  f.extraRoots = [legacy];
  f.browser.sync(2);
  assert.ok(f.browser.recordedRoots().includes(legacy));
  f.extraRoots = [];
  f.dirty.add(legacy);
  f.calls.length = 0;
  assert.throws(() => f.browser.sync(2), /legacy has local changes/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("incomplete dependency discovery cannot publish a successful sync stamp", (t) => {
  const f = new Fixture(t);
  f.omitSourceRoot = true;
  assert.throws(() => f.browser.sync(2), /dependency discovery is incomplete/);
  assert.equal(existsSync(join(f.layout.root, SYNC_STAMP)), false);
});

test("an existing checkout with missing dependency history is preserved", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  rmSync(join(f.layout.root, ".uw-dependencies.json"));
  f.calls.length = 0;
  assert.throws(() => f.browser.sync(2), /Dependency history is missing/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("build and smoke use an isolated profile and keep the sandbox enabled", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); f.browser.build(2); f.browser.smoke(30_000);
  const command = f.calls.findLast(({ command }) => command[0] === f.layout.binary)!.command;
  const profile = command.find((arg) => arg.startsWith("--user-data-dir="))!.slice("--user-data-dir=".length);
  assert.equal(existsSync(profile), false);
  assert.ok(command.includes("--headless")); assert.equal(command.includes("--no-sandbox"), false);
});

test("smoke requires the executed JavaScript result and cleans up on failure", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); f.browser.build(2);
  f.dom = "<body><script>document.body.setAttribute('data-uw-smoke', 'passed')</script></body>";
  assert.throws(() => f.browser.smoke(30_000), /JavaScript\/DOM/);
  const command = f.calls.findLast(({ command }) => command[0] === f.layout.binary)!.command;
  const profile = command.find((arg) => arg.startsWith("--user-data-dir="))!.slice("--user-data-dir=".length);
  assert.equal(existsSync(profile), false);
});

function invoke(...args: string[]) {
  return spawnSync(process.execPath, [join(REPO, "scripts/chromium.ts"), ...args], { encoding: "utf8", timeout: 15_000 });
}

test("every command's help has examples", () => {
  for (const command of [[], ["root"], ["doctor"], ["sync"], ["build"], ["smoke"]]) {
    const result = invoke(...command, "--help");
    assert.equal(result.status, 0, result.stderr); assert.match(result.stdout, /Examples:/);
  }
});

test("invalid job counts and checkout paths fail before host checks", () => {
  for (const jobs of ["0", "-1", "lots"]) {
    const result = invoke("build", `--jobs=${jobs}`);
    assert.equal(result.status, 2); assert.match(result.stderr, /positive integer/);
  }
  const result = invoke("sync", "--checkout-dir", "/tmp/uw build");
  assert.equal(result.status, 2); assert.match(result.stderr, /must not contain spaces/);
});

test("root command resolves a missing path without creating it", (t) => {
  const path = join(temporary(t), "checkout");
  const result = invoke("root", "--checkout-dir", path);
  assert.equal(result.status, 0, result.stderr); assert.equal(result.stdout.trim(), layoutAt(path).root);
  assert.equal(existsSync(path), false);
});
