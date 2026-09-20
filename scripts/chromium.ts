#!/usr/bin/env node
/** Build the pinned Chromium baseline with upstream tools. */

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import {
  accessSync, constants, existsSync, mkdirSync, mkdtempSync, readFileSync,
  readdirSync, realpathSync, rmSync, statfsSync, statSync, writeFileSync,
} from "node:fs";
import { homedir, tmpdir, totalmem } from "node:os";
import { delimiter, dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";

export const REPO = fileURLToPath(new URL("../", import.meta.url));
export const CHROMIUM_URL = "https://chromium.googlesource.com/chromium/src.git";
export const DEPOT_TOOLS_URL = "https://chromium.googlesource.com/chromium/tools/depot_tools.git";
export const MARKER = ".uw-chromium-root";
export const MARKER_TEXT = "uw browser Chromium build checkout\n";
export const SYNC_STAMP = ".uw-sync.json";
const CONFIG_SEAL = ".uw-gclient.sha256";
const DEPENDENCY_HISTORY = ".uw-dependencies.json";
const ROOT_PREFIX = "UW_CHECKOUT_ROOT=";
const OUTPUT = "out/uw";
export const GIB = 1024 ** 3;

export interface Pins {
  version: string;
  chromium_revision: string;
  depot_tools_revision: string;
  macos_sdk_min: string;
}

export interface Layout {
  readonly root: string;
  readonly tools: string;
  readonly source: string;
  readonly binary: string;
}

export interface CommandOptions {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  timeout?: number;
  input?: string;
  inherit?: boolean;
}

export type Runner = (command: readonly string[], options?: CommandOptions) => string;

export interface Runtime {
  run: Runner;
  env: NodeJS.ProcessEnv;
  platform: string;
  arch: string;
  nodeVersion: string;
  memoryBytes: number;
  freeBytes: (path: string) => number;
  log: (message: string) => void;
}

export class BuildError extends Error {
  readonly exitCode: number;

  constructor(message: string, exitCode = 1) {
    super(message);
    this.name = "BuildError";
    this.exitCode = exitCode;
  }
}

export function execute(command: readonly string[], options: CommandOptions = {}): string {
  const [program, ...args] = command;
  if (!program) throw new BuildError("An executable is required");
  const result = spawnSync(program, args, {
    cwd: options.cwd,
    env: options.env,
    encoding: "utf8",
    input: options.input,
    stdio: options.inherit ? "inherit" : "pipe",
    timeout: options.timeout ?? (options.inherit ? undefined : 30_000),
    maxBuffer: 256 * 1024 * 1024,
  });
  if (result.error || result.status !== 0) {
    const detail = result.error?.message ?? result.stderr?.trim() ?? result.signal ?? "";
    throw new BuildError(
      `${program} failed${result.status === null ? "" : ` with exit ${result.status}`}: ${detail}`,
      result.signal === "SIGINT" ? 130 : 1,
    );
  }
  return result.stdout?.trim() ?? "";
}

export function nativeRuntime(): Runtime {
  return {
    run: execute, env: { ...process.env }, platform: process.platform, arch: process.arch,
    nodeVersion: process.versions.node, memoryBytes: totalmem(), log: console.log,
    freeBytes: (path) => { const stat = statfsSync(path); return stat.bavail * stat.bsize; },
  };
}

export function nearestExisting(path: string): string {
  while (!existsSync(path)) path = dirname(path);
  return path;
}

export function checkoutPath(value: string): string {
  const expanded = value === "~" ? homedir() : value.startsWith("~/") ? join(homedir(), value.slice(2)) : value;
  const absolute = resolve(expanded);
  const existing = nearestExisting(absolute);
  const path = resolve(realpathSync(existing), relative(existing, absolute));
  if (/\s/.test(path)) throw new BuildError("Chromium checkout paths must not contain spaces or whitespace", 2);
  return path;
}

export function layoutAt(root: string): Layout {
  const normalized = checkoutPath(root);
  return {
    root: normalized, tools: join(normalized, "depot_tools"), source: join(normalized, "src"),
    binary: join(normalized, "src", OUTPUT, "Chromium.app/Contents/MacOS/Chromium"),
  };
}

export function loadPins(path = join(REPO, "chromium/pins.json")): Pins {
  const raw: unknown = JSON.parse(readFileSync(path, "utf8"));
  const patterns = {
    version: /^\d+\.\d+\.\d+\.\d+$/,
    chromium_revision: /^[0-9a-f]{40}$/,
    depot_tools_revision: /^[0-9a-f]{40}$/,
    macos_sdk_min: /^\d+\.\d+(?:\.\d+)?$/,
  };
  if (typeof raw !== "object" || raw === null || Array.isArray(raw) ||
      Object.keys(raw).sort().join() !== Object.keys(patterns).sort().join()) {
    throw new BuildError(`${path}: expected exactly ${Object.keys(patterns).join(", ")}`);
  }
  const values = raw as Record<string, unknown>;
  const field = (key: keyof Pins): string => {
    const value = values[key];
    if (typeof value !== "string" || !patterns[key].test(value)) throw new BuildError(`${path}: invalid ${key}`);
    return value;
  };
  return {
    version: field("version"), chromium_revision: field("chromium_revision"),
    depot_tools_revision: field("depot_tools_revision"), macos_sdk_min: field("macos_sdk_min"),
  };
}

function atLeast(version: string, minimum: string): boolean {
  if (!/^\d+(?:\.\d+){1,2}$/.test(version)) return false;
  const actual = version.split(".").map(Number);
  const required = minimum.split(".").map(Number);
  for (let index = 0; index < 3; index++) {
    const difference = (actual[index] ?? 0) - (required[index] ?? 0);
    if (difference !== 0) return difference > 0;
  }
  return true;
}

function within(root: string, path: string): boolean {
  const child = relative(root, path);
  return child !== ".." && !child.startsWith(`..${sep}`) && !isAbsolute(child);
}

export function requireOwnedRoot(layout: Layout, create = false): void {
  const marker = join(layout.root, MARKER);
  if (existsSync(marker) && readFileSync(marker, "utf8") === MARKER_TEXT) return;
  if (create && (!existsSync(layout.root) || readdirSync(layout.root).length === 0)) {
    mkdirSync(layout.root, { recursive: true });
    writeFileSync(marker, MARKER_TEXT);
    return;
  }
  throw new BuildError(`${layout.root} is not a managed uw checkout. Choose a dedicated empty --checkout-dir and run sync.`);
}

export function requireCleanWorktrees(layout: Layout, run: Runner = execute, extraRoots: string[] = []): void {
  const pending = [layout.source, layout.tools, ...extraRoots];
  const checked = new Set<string>();
  while (pending.length) {
    const path = checkoutPath(pending.pop()!);
    if (!within(layout.root, path)) throw new BuildError(`Dependency path escapes the checkout: ${path}`);
    if (checked.has(path) || !existsSync(join(path, ".git"))) continue;
    checked.add(path);
    const git = ["git", "-C", path];
    // Allow dependency revision drift after interruption; inspect each worktree
    // separately and still reject staged gitlink edits.
    const work = run([...git, "status", "--porcelain", "--untracked-files=normal", "--ignore-submodules=all"], { timeout: 300_000 });
    const staged = run([...git, "diff", "--cached", "--name-only", "--ignore-submodules=none"]);
    if (work || staged) throw new BuildError(`${path} has local changes. Preserve them before running sync.`);
    for (const entry of run([...git, "ls-files", "--stage", "-z"], { timeout: 300_000 }).split("\0")) {
      if (entry.startsWith("160000 ")) {
        const separator = entry.indexOf("\t");
        if (separator < 0) throw new BuildError(`Invalid gitlink entry in ${path}`);
        pending.push(join(path, entry.slice(separator + 1)));
      }
    }
  }
}

export class Chromium {
  readonly layout: Layout;
  readonly pins: Pins;
  readonly runtime: Runtime;

  constructor(layout: Layout, pins: Pins, runtime = nativeRuntime()) {
    this.layout = layout;
    this.pins = pins;
    this.runtime = runtime;
  }

  environment(): NodeJS.ProcessEnv {
    return {
      ...this.runtime.env, PATH: `${this.layout.tools}${delimiter}${this.runtime.env.PATH ?? ""}`,
      DEPOT_TOOLS_UPDATE: "0", DEPOT_TOOLS_METRICS: "0", DEPOT_TOOLS_DIR: this.layout.tools,
      DEPOT_TOOLS_BOOTSTRAP_PYTHON3: "1", GIT_TERMINAL_PROMPT: "0",
    };
  }

  capture(command: readonly string[], options: CommandOptions = {}): string {
    return this.runtime.run(command, { env: this.runtime.env, ...options });
  }

  run(command: readonly string[], cwd: string): void {
    this.runtime.log(`[${cwd}] ${command.map((value) => JSON.stringify(value)).join(" ")}`);
    this.runtime.run(command, { cwd, env: this.environment(), inherit: true });
  }

  doctor(): boolean {
    let failures = 0;
    const report = (name: string, ok: boolean, detail: string): void => {
      if (!ok) failures++;
      this.runtime.log(`${ok ? "OK" : "FAIL"} ${name}: ${detail}`);
    };
    const probe = (name: string, operation: () => string, hint: string): void => {
      try { report(name, true, operation()); } catch { report(name, false, hint); }
    };
    report("Node.js", atLeast(this.runtime.nodeVersion, "24.13.0"), this.runtime.nodeVersion);
    report("Platform", this.runtime.platform === "darwin", `${this.runtime.platform} ${this.runtime.arch}`);
    if (this.runtime.platform !== "darwin") return false;
    report("Architecture", ["arm64", "x64"].includes(this.runtime.arch), this.runtime.arch);
    for (const tool of ["git", "curl"]) {
      probe(tool, () => this.capture([tool, "--version"]).split("\n")[0] ?? "",
        `${tool} is not usable on PATH; check \`${tool} --version\` in your shell`);
    }
    this.runtime.log(`Checkout: ${this.layout.root}`);
    probe("Xcode", () => {
      const version = this.capture(["xcodebuild", "-version"]);
      this.capture(["xcodebuild", "-checkFirstLaunchStatus"]);
      return version.replaceAll("\n", ", ");
    }, "install/open full Xcode and finish first-launch setup; select it with DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer");
    try {
      const sdk = this.capture(["xcrun", "--sdk", "macosx", "--show-sdk-version"]);
      report("macOS SDK", atLeast(sdk, this.pins.macos_sdk_min), `${sdk}; required >= ${this.pins.macos_sdk_min}`);
    } catch { report("macOS SDK", false, "xcrun could not locate a usable macOS SDK"); }
    probe("Metal compiler", () => this.capture(["xcrun", "metal", "--version"]).split("\n")[0] ?? "",
      "install the Metal Toolchain with `xcodebuild -downloadComponent MetalToolchain`");
    const existing = nearestExisting(this.layout.root);
    const free = this.runtime.freeBytes(existing) / GIB;
    const required = existsSync(join(this.layout.source, "DEPS")) ? 50 : 100;
    report("Free disk", free >= required, `${free.toFixed(1)} GiB at ${existing}; required >= ${required} GiB`);
    try {
      const device = this.capture(["df", "-P", existing]).split("\n").at(-1)?.split(/\s+/)[0];
      if (!device) throw new BuildError("df returned no filesystem");
      const plist = this.capture(["diskutil", "info", "-plist", device]);
      const filesystem = this.capture(["plutil", "-extract", "FilesystemType", "raw", "-o", "-", "--", "-"], { input: plist });
      report("Filesystem", filesystem === "apfs", `${filesystem} on ${device}; APFS required`);
    } catch { report("Filesystem", false, "could not verify the checkout volume with diskutil/plutil"); }
    report("Memory", true, `${(this.runtime.memoryBytes / GIB).toFixed(0)} GiB; start with --jobs 2 on a 16 GiB Mac`);
    return failures === 0;
  }

  requireHost(): void {
    if (!this.doctor()) throw new BuildError("Preflight failed. See README.md#build-host-requirements before retrying.");
  }

  verifyRepo(path: string, url: string, revision?: string): void {
    if (!existsSync(join(path, ".git")) || !statSync(join(path, ".git")).isDirectory()) {
      throw new BuildError(`${path}: expected a Git checkout; run sync in a dedicated checkout root`);
    }
    const git = ["git", "-C", path];
    if (checkoutPath(this.capture([...git, "rev-parse", "--show-toplevel"])) !== checkoutPath(path)) {
      throw new BuildError(`${path}: unexpected Git root`);
    }
    if (this.capture([...git, "remote", "get-url", "origin"]) !== url) throw new BuildError(`${path}: origin must be ${url}`);
    if (revision && this.capture([...git, "rev-parse", "HEAD"]) !== revision) {
      throw new BuildError(`${path}: revision differs from chromium/pins.json; run sync`);
    }
  }

  configHash(): string {
    return createHash("sha256").update(readFileSync(join(this.layout.root, ".gclient"))).digest("hex");
  }

  verifyConfig(): void {
    const seal = join(this.layout.root, CONFIG_SEAL);
    if (!existsSync(seal) || readFileSync(seal, "utf8") !== this.configHash()) {
      throw new BuildError("The upstream-generated .gclient configuration is unrecorded or changed; preserve it and use a dedicated checkout.");
    }
  }

  verifyCheckout(requireSynced = true): void {
    requireOwnedRoot(this.layout);
    const stamp = join(this.layout.root, SYNC_STAMP);
    if (requireSynced && (!existsSync(stamp) || readFileSync(stamp, "utf8") !== JSON.stringify(this.pins))) {
      throw new BuildError("Sync has not completed for the current pins. Run sync before building.");
    }
    this.verifyConfig();
    this.verifyRepo(this.layout.tools, DEPOT_TOOLS_URL, this.pins.depot_tools_revision);
    this.verifyRepo(this.layout.source, CHROMIUM_URL, this.pins.chromium_revision);
    const versionFile = join(this.layout.source, "chrome/VERSION");
    const fields = new Map(readFileSync(versionFile, "utf8").split("\n").filter((line) => line.includes("=")).map((line) => {
      const separator = line.indexOf("=");
      return [line.slice(0, separator), line.slice(separator + 1)] as const;
    }));
    const actual = ["MAJOR", "MINOR", "BUILD", "PATCH"].map((key) => fields.get(key) ?? "").join(".");
    if (actual !== this.pins.version) throw new BuildError(`${versionFile}: version ${actual} does not match pin ${this.pins.version}`);
  }

  dependencyRoots(): string[] {
    if (!existsSync(join(this.layout.root, ".gclient_entries"))) return [];
    // Let upstream read its own configuration instead of evaluating it here.
    const output = this.capture([
      join(this.layout.tools, "gclient"), "recurse", "--no-progress", "--jobs", "1", "--scm=git",
      process.execPath, "-e", `console.log('${ROOT_PREFIX}' + JSON.stringify(process.cwd()))`,
    ], { cwd: this.layout.root, env: this.environment(), timeout: 300_000 });
    const roots = output.split("\n").filter((line) => line.startsWith(ROOT_PREFIX)).map((line) => {
      const path: unknown = JSON.parse(line.slice(ROOT_PREFIX.length));
      if (typeof path !== "string") throw new BuildError("Invalid dependency path from gclient");
      return checkoutPath(path);
    });
    if (!roots.includes(this.layout.source)) throw new BuildError("gclient did not report the source checkout; dependency discovery is incomplete");
    return roots;
  }

  recordedRoots(): string[] {
    const history = join(this.layout.root, DEPENDENCY_HISTORY);
    if (!existsSync(history)) {
      if (existsSync(join(this.layout.root, ".gclient_entries"))) {
        throw new BuildError("Dependency history is missing. Preserve this checkout and use a dedicated empty checkout directory.");
      }
      return [];
    }
    const paths: unknown = JSON.parse(readFileSync(history, "utf8"));
    if (!Array.isArray(paths) || paths.some((path: unknown) => typeof path !== "string")) {
      throw new BuildError(`${history}: expected recorded dependency paths`);
    }
    return (paths as string[]).map((path) => checkoutPath(resolve(this.layout.root, path)));
  }

  rememberRoots(roots: string[]): void {
    const paths = [...new Set(roots.map((path) => checkoutPath(path)))].sort();
    for (const path of paths) {
      if (!within(this.layout.root, path)) throw new BuildError(`Dependency path escapes the checkout: ${path}`);
    }
    writeFileSync(join(this.layout.root, DEPENDENCY_HISTORY), JSON.stringify(paths.map((path) => relative(this.layout.root, path))));
  }

  sync(jobs: number): void {
    this.requireHost();
    requireOwnedRoot(this.layout, true);
    const config = join(this.layout.root, ".gclient");
    if (existsSync(config)) this.verifyConfig();
    if (existsSync(this.layout.source)) this.verifyRepo(this.layout.source, CHROMIUM_URL);
    if (existsSync(this.layout.tools)) this.verifyRepo(this.layout.tools, DEPOT_TOOLS_URL);
    const previousRoots = this.recordedRoots();
    const check = (roots: string[] = []): void => requireCleanWorktrees(this.layout, this.capture.bind(this), roots);
    check(previousRoots);
    if (!existsSync(this.layout.tools)) {
      this.run(["git", "clone", "--depth", "1", DEPOT_TOOLS_URL, this.layout.tools], this.layout.root);
    }
    rmSync(join(this.layout.root, SYNC_STAMP), { force: true });
    this.run(["git", "fetch", "--depth", "1", "origin", this.pins.depot_tools_revision], this.layout.tools);
    this.run(["git", "checkout", "--detach", this.pins.depot_tools_revision], this.layout.tools);
    this.run([join(this.layout.tools, "ensure_bootstrap")], this.layout.tools);
    // Upstream's bootstrap can return zero after a failed package download.
    const runtime = readFileSync(join(this.layout.tools, "python3_bin_reldir.txt"), "utf8").trim();
    const interpreter = resolve(this.layout.tools, runtime, "python3");
    if (!within(this.layout.tools, interpreter)) throw new BuildError("Invalid upstream runtime path");
    accessSync(interpreter, constants.X_OK);
    if (!existsSync(config)) {
      this.run([join(this.layout.tools, "gclient"), "config", "--name", "src", "--unmanaged", CHROMIUM_URL], this.layout.root);
      writeFileSync(join(this.layout.root, CONFIG_SEAL), this.configHash());
    }
    const knownRoots = [...previousRoots, ...this.dependencyRoots()];
    check(knownRoots);
    // Keep old roots across failed updates and removals from the current DEPS
    // graph, without interpreting upstream's configuration language.
    this.rememberRoots(knownRoots);
    this.run([join(this.layout.tools, "gclient"), "sync", "--no-history",
      "--revision", `src@${this.pins.chromium_revision}`, "--jobs", String(jobs)], this.layout.root);
    this.verifyCheckout(false);
    this.rememberRoots([...knownRoots, ...this.dependencyRoots()]);
    writeFileSync(join(this.layout.root, SYNC_STAMP), JSON.stringify(this.pins));
    this.runtime.log(`Synced Chromium ${this.pins.version} at ${this.pins.chromium_revision}`);
  }

  build(jobs: number): void {
    this.requireHost();
    this.verifyCheckout();
    const args = readFileSync(join(REPO, "chromium/args.gn"), "utf8") +
      (this.runtime.arch === "arm64" ? "\nuse_lld = false\n" : "");
    this.run([join(this.layout.tools, "gn"), "gen", OUTPUT, `--args=${args}`, "--fail-on-unused-args"], this.layout.source);
    this.run([join(this.layout.tools, "autoninja"), "-C", OUTPUT, `-j${jobs}`, "chrome"], this.layout.source);
    if (!existsSync(this.layout.binary)) throw new BuildError(`Build returned successfully but ${this.layout.binary} is missing`);
    this.runtime.log(`Built ${this.layout.binary}`);
  }

  smoke(timeout: number): void {
    this.verifyCheckout();
    if (!existsSync(this.layout.binary)) throw new BuildError(`${this.layout.binary} is missing. Run build first.`);
    const version = this.capture([this.layout.binary, "--version"], { timeout });
    if (!version.split(/\s+/).includes(this.pins.version)) {
      throw new BuildError(`Built browser reported ${version}; expected version ${this.pins.version}`);
    }
    const html = "<html><body><script>document.body.setAttribute('data-uw-smoke', 'passed');" +
      "document.title = 'uw Chromium smoke';</script></body></html>";
    const profile = mkdtempSync(join(tmpdir(), "uw-chromium-smoke-"));
    let dom: string;
    try {
      dom = this.capture([
        this.layout.binary, "--headless", "--incognito", "--no-first-run", "--no-default-browser-check",
        "--disable-background-networking", "--disable-component-update", "--use-mock-keychain",
        "--disable-features=DialMediaRouteProvider", `--user-data-dir=${profile}`,
        "--dump-dom", `data:text/html,${encodeURIComponent(html)}`,
      ], { timeout });
    } finally { rmSync(profile, { recursive: true, force: true }); }
    if (!dom.includes('data-uw-smoke="passed"')) throw new BuildError("Browser started but the JavaScript/DOM smoke check failed");
    this.runtime.log(`${version}\nPASS: headless navigation and JavaScript in an isolated profile`);
  }
}

const descriptions = {
  root: "Print the resolved checkout path without creating it.",
  doctor: "Check the macOS host without creating a checkout or downloading sources.",
  sync: "Download the pinned Chromium source and dependencies; refuse local source edits.",
  build: "Generate build files and compile the chrome target from the pinned checkout.",
  smoke: "Verify the built version and run an isolated headless JavaScript/DOM check.",
} as const;

function help(command?: keyof typeof descriptions): string {
  const usage = command ?? "<root|doctor|sync|build|smoke>";
  return `${command ? descriptions[command] : "Build the pinned Chromium baseline with upstream tools."}\n\n` +
    `Usage: node scripts/chromium.ts ${usage} [options]\n` +
    "  --checkout-dir PATH  Dedicated root; defaults to UW_CHROMIUM_ROOT or .chromium/\n" +
    (command === "sync" || command === "build" ? "  --jobs NUMBER        Parallel jobs; default: 2\n" : "") +
    (command === "smoke" ? "  --timeout SECONDS    Browser command timeout; default: 120\n" : "") +
    "  --help, -h           Show this help\n\nExamples:\n" +
    `  node scripts/chromium.ts ${command ?? "doctor"}\n` +
    `  node scripts/chromium.ts ${command ?? "sync"} --checkout-dir /Volumes/Build/uw-chromium\n`;
}

function positiveInteger(value: string, flag: string): number {
  const number = Number(value);
  if (!/^\d+$/.test(value) || !Number.isSafeInteger(number) || number < 1) {
    throw new BuildError(`${flag} must be a positive integer`, 2);
  }
  return number;
}

export function main(argv = process.argv.slice(2)): number {
  try {
    if (!argv.length || argv[0] === "--help" || argv[0] === "-h") { console.log(help()); return 0; }
    const command = argv[0];
    if (!command || !Object.hasOwn(descriptions, command)) throw new BuildError("Choose root, doctor, sync, build, or smoke. Use --help for examples.", 2);
    const name = command as keyof typeof descriptions;
    const { values, positionals } = parseArgs({
      args: argv.slice(1), strict: true, allowPositionals: false,
      options: {
        help: { type: "boolean", short: "h" }, "checkout-dir": { type: "string" },
        ...(name === "sync" || name === "build" ? { jobs: { type: "string" as const } } : {}),
        ...(name === "smoke" ? { timeout: { type: "string" as const } } : {}),
      },
    });
    if (positionals.length) throw new BuildError("Unexpected positional argument", 2);
    if (values.help) { console.log(help(name)); return 0; }
    const jobs = positiveInteger(String(values.jobs ?? "2"), "--jobs");
    const timeout = positiveInteger(String(values.timeout ?? "120"), "--timeout") * 1000;
    const layout = layoutAt(String(values["checkout-dir"] ?? (process.env.UW_CHROMIUM_ROOT || join(REPO, ".chromium"))));
    if (name === "root") { console.log(layout.root); return 0; }
    const chromium = new Chromium(layout, loadPins());
    if (name === "doctor") return chromium.doctor() ? 0 : 1;
    if (name === "sync") chromium.sync(jobs);
    if (name === "build") chromium.build(jobs);
    if (name === "smoke") chromium.smoke(timeout);
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    if (error instanceof BuildError) return error.exitCode;
    return error instanceof Error && "code" in error && String(error.code).startsWith("ERR_PARSE_ARGS") ? 2 : 1;
  }
}

if (import.meta.main) process.exitCode = main();
