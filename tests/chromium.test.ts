import assert from "node:assert/strict";
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import { test } from "node:test";
import type { TestContext } from "node:test";
import {
  BuildError, CHROMIUM_URL, Chromium, DEPOT_TOOLS_URL, GIB, MARKER, REPO,
  SYNC_STAMP, WINDOW_VM, layoutAt, loadPins, requireOwnedRoot,
} from "../scripts/chromium.ts";
import type { CommandOptions, Pins, Runtime } from "../scripts/chromium.ts";

const pins: Pins = {
  version: "1.2.3.4", chromium_revision: "a".repeat(40),
  depot_tools_revision: "b".repeat(40), macos_sdk_min: "26.5",
  macos_vm_ipsw: "https://updates.cdn-apple.com/fixture/UniversalMac_27.0.1_26A434_Restore.ipsw",
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
  metalReady = true;
  curlReady = true;
  sdk = "26.5";
  filesystem = "apfs";
  bootstrapSucceeds = true;
  bootstrapped = false;
  failSync = false;
  extraRoots: string[] = [];
  omitSourceRoot = false;
  dom = '<html><body data-uw-smoke="passed"></body></html>';
  readonly started: (readonly string[])[] = [];
  vms = [{ Name: WINDOW_VM, State: "stopped" }];
  guestBuild = "26A434";
  failWindowTests = false;

  constructor(t: TestContext) {
    this.layout = layoutAt(join(temporary(t), "checkout"));
    this.runtime = {
      platform: "darwin", arch: "arm64", nodeVersion: "24.13.0", memoryBytes: 16 * GIB,
      env: {}, freeBytes: () => this.free, log: (line) => this.messages.push(line),
      run: (command, options = {}) => this.execute(command, options),
      start: (command) => this.started.push(command),
      addresses: () => [{ address: "192.168.1.5", netmask: "255.255.255.0" }, { address: "192.168.64.1", netmask: "255.255.255.0" }],
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
    if (program === "xcrun") {
      if (operation === "metal") {
        if (!this.metalReady) throw new BuildError("Metal Toolchain unavailable");
        return "Apple metal fixture version";
      }
      return this.sdk;
    }
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
      if (args[0] === "diff") return this.dirty.has(path) ? "local source edits" : "";
      if (args[0] === "ls-files" || args[0] === "read-tree" || args[0] === "apply") return "";
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
    if (program === "tart") {
      if (operation === "list") return JSON.stringify(this.vms);
      return operation === "ip" ? "192.168.64.2" : "";
    }
    if (program === "ssh") {
      if (command.at(-1) === "sw_vers -buildVersion") return this.guestBuild;
      if (options.inherit && this.failWindowTests) throw new BuildError("browser_tests failed");
      return "";
    }
    if (basename(program) === "gn") return "";
    if (basename(program) === "autoninja") {
      mkdirSync(dirname(this.layout.binary), { recursive: true }); writeFileSync(this.layout.binary, "browser fixture"); return "";
    }
    if (program === this.layout.binary) return operation === "--version" ? `uw ${pins.version}` : this.dom;
    if (operation?.startsWith("--gtest_filter=")) return "";
    throw new Error(`Unexpected command: ${JSON.stringify(command)}`);
  }
}

test("repository pins are valid and invalid pins fail before commands", (t) => {
  assert.match(loadPins().chromium_revision, /^[a-f0-9]{40}$/);
  const path = join(temporary(t), "pins.json");
  for (const invalid of [
    { chromium_revision: "main" }, { depot_tools_revision: "latest" },
    { version: 123 }, { macos_sdk_min: "latest" }, { macos_vm_ipsw: "latest" }, { extra: "value" },
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
  f.xcodeReady = false; f.metalReady = false; f.free = 13 * GIB; f.sdk = "15.0"; f.filesystem = "exfat"; f.curlReady = false;
  assert.equal(f.browser.doctor(), false);
  for (const label of ["Xcode", "Metal compiler", "Free disk", "macOS SDK", "Filesystem", "curl"]) {
    assert.ok(f.messages.some((line) => line.startsWith(`FAIL ${label}:`)));
  }
  assert.equal(existsSync(f.layout.root), false);
  assert.throws(() => f.browser.sync(2), /Preflight failed/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
});

test("high job counts fail fast on small hosts before mutation", (t) => {
  const f = new Fixture(t);
  assert.throws(() => f.browser.build(8), /--jobs 2/);
  assert.throws(() => f.browser.sync(8), /--jobs 2/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
  f.browser.sync(2); f.browser.build(2);
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

test("a new VM pin keeps a synced checkout buildable", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2);
  const moved = new Chromium(f.layout, { ...pins, macos_vm_ipsw: pins.macos_vm_ipsw.replace("26A434", "26B100") }, f.runtime);
  moved.build(2);
});

function windowFixture(t: TestContext): Fixture {
  const f = new Fixture(t);
  const tests = join(f.layout.source, "out/uw/browser_tests");
  mkdirSync(dirname(tests), { recursive: true });
  writeFileSync(tests, "browser_tests fixture");
  const home = temporary(t);
  mkdirSync(join(home, ".ssh"));
  writeFileSync(join(home, ".ssh", WINDOW_VM), "key fixture");
  f.runtime.env.HOME = home;
  return f;
}

function windowTestRun(f: Fixture): string {
  return f.calls.find(({ options }) => options.inherit)!.command.at(-1)!;
}

test("window tests run browser_tests in the VM and always stop it", (t) => {
  const f = windowFixture(t);
  f.browser.windowTests([]);
  assert.deepEqual(f.started[0]!.slice(0, 4), ["tart", "run", WINDOW_VM, "--no-graphics"]);
  assert.ok(f.started[0]!.includes(`--dir=src:${f.layout.source}:ro`));
  assert.match(windowTestRun(f), /browser_tests --run-manual --headful-mode '--gtest_filter=\*\.MANUAL_\*'$/);
  for (const { command } of f.calls.filter(({ command }) => command[0] === "ssh")) {
    assert.deepEqual(command.slice(1, 3), ["-b", "192.168.64.1"]);
  }
  assert.deepEqual(f.calls.at(-1)!.command, ["tart", "stop", WINDOW_VM]);
  assert.equal(existsSync(join(f.layout.root, ".uw-window-tests.lock")), false);

  f.failWindowTests = true;
  f.calls.length = 0;
  assert.throws(() => f.browser.windowTests(["--gtest_filter=A.MANUAL_B", "--gtest_repeat=100"]), /browser_tests failed/);
  assert.match(windowTestRun(f), /--headful-mode '--gtest_filter=A\.MANUAL_B' '--gtest_repeat=100'$/);
  assert.deepEqual(f.calls.at(-1)!.command, ["tart", "stop", WINDOW_VM]);
});

test("window tests refuse a VM on another macOS build", (t) => {
  const f = windowFixture(t);
  f.guestBuild = "25A100";
  assert.throws(() => f.browser.windowTests([]), /runs macOS build 25A100; chromium\/pins\.json pins 26A434/);
  assert.equal(f.calls.some(({ options }) => options.inherit), false);
  assert.deepEqual(f.calls.at(-1)!.command, ["tart", "stop", WINDOW_VM]);
});

test("window tests point to the setup when the VM or its key is missing", (t) => {
  const f = windowFixture(t);
  rmSync(join(f.runtime.env.HOME!, ".ssh", WINDOW_VM));
  assert.throws(() => f.browser.windowTests([]), /SSH key .+ is missing\. Set it up as README\.md#real-window-tests/);
  f.vms = [];
  assert.throws(() => f.browser.windowTests([]), /uw-window-tests VM is missing/);
  assert.equal(f.started.length, 0);
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
  assert.ok(command.includes("--headless")); assert.ok(command.includes("--incognito"));
  assert.equal(command.includes("--no-sandbox"), false);
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

test("verify builds, smoke-tests, then runs the focused test suites", (t) => {
  const f = new Fixture(t);
  f.browser.sync(2); f.calls.length = 0;
  f.browser.verify(2, 30_000);
  const ran = f.calls.filter(({ options }) => options.inherit).map(({ command }) => command);
  const compiled = ran.filter((command) => basename(command[0]!) === "autoninja").map((command) => command.slice(4));
  assert.deepEqual(compiled, [["chrome"], ["unit_tests", "browser_tests"]]);
  const suites = ran.filter((command) => command[1]?.startsWith("--gtest_filter="));
  assert.deepEqual(suites.map((command) => basename(command[0]!)), ["unit_tests", "browser_tests"]);
  assert.match(suites[1]![1]!, /AutomaticOrganizerBrowserTest\.\*/);
  assert.ok(f.calls.findLastIndex(({ command }) => command.includes("--dump-dom")) < f.calls.findIndex(({ command }) => command === suites[0]));
});

function invoke(...args: string[]) {
  return spawnSync(process.execPath, [join(REPO, "scripts/chromium.ts"), ...args], { encoding: "utf8", timeout: 15_000 });
}

test("every command's help has examples", () => {
  for (const command of [[], ["root"], ["doctor"], ["sync"], ["prepare"], ["build"], ["smoke"], ["verify"], ["window-tests"]]) {
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
