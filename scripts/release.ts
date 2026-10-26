#!/usr/bin/env node
import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { delimiter, join } from "node:path";
import { parseArgs } from "node:util";
import { BuildError, REPO, nativeRuntime } from "./chromium.ts";
import type { Runtime } from "./chromium.ts";

export const TEAM_ID = "24MD6K3S56";
export const NOTARY_PROFILE = "uw-notary";
export const DMG = "uw-macos-arm64.dmg";
const BRANCH = "origin/main";
const NETWORK_TIMEOUT = 300_000;

export interface ReleaseOptions {
  version: string;
  sha?: string;
  worktree: string;
  jobs: number;
}

export function newer(version: string, than: string): boolean {
  const [a, b] = [version, than].map((value) => value.replace(/^v/, "").split(".").map(Number));
  for (let index = 0; index < 3; index++) {
    if (a![index] !== b![index]) return a![index]! > b![index]!;
  }
  return false;
}

export function draftNotes(version: string, chromium: string, subjects: readonly string[] | undefined): string {
  const changes = subjects === undefined
    ? "The first public preview.\n"
    : subjects.filter((subject) => /^(feat|fix)(\(|:)/.test(subject)).map((subject) => `- ${subject}\n`).join("");
  return `uw ${version} is an early preview for Macs with Apple silicon on macOS 13 or later, built on Chromium ${chromium}. ` +
    "It may break, and it comes with no support promise.\n\n## Changes\n\n" + changes;
}

export class Release {
  readonly options: ReleaseOptions;
  readonly runtime: Runtime;
  private readonly source: string;
  private readonly tools: string;

  constructor(options: ReleaseOptions, runtime = nativeRuntime()) {
    this.options = options;
    this.runtime = runtime;
    this.source = join(options.worktree, ".chromium/src");
    this.tools = join(options.worktree, ".chromium/depot_tools");
  }

  private capture(command: readonly string[], cwd = REPO, timeout?: number): string {
    return this.runtime.run(command, { cwd, env: this.environment(), timeout });
  }

  private step(command: readonly string[], cwd: string): void {
    this.runtime.log(`[${cwd}] ${command.map((value) => JSON.stringify(value)).join(" ")}`);
    this.runtime.run(command, { cwd, env: this.environment(), inherit: true });
  }

  private environment(): NodeJS.ProcessEnv {
    return { ...this.runtime.env, PATH: `${this.tools}${delimiter}${this.runtime.env.PATH ?? ""}`, DEPOT_TOOLS_UPDATE: "0" };
  }

  private succeeds(command: readonly string[]): boolean {
    try { this.capture(command); return true; } catch { return false; }
  }

  identity(): string {
    const line = this.capture(["security", "find-identity", "-v", "-p", "codesigning"]).split("\n")
      .find((entry) => entry.includes("\"Developer ID Application:") && entry.includes(`(${TEAM_ID})"`));
    const hash = line?.match(/\b[0-9A-F]{40}\b/)?.[0];
    if (!hash) throw new BuildError(`No Developer ID Application certificate for team ${TEAM_ID} in the keychain`);
    return hash;
  }

  /** Builds, signs, and notarizes the release, then publishes it as a draft GitHub release. */
  publish(): string {
    const { version, worktree, jobs } = this.options;
    if (!/^\d+\.\d+\.\d+$/.test(version)) throw new BuildError("The version must look like 0.1.0", 2);
    const tag = `v${version}`;
    this.capture(["git", "fetch", "--tags", "origin", "main"], REPO, NETWORK_TIMEOUT);
    const sha = this.capture(["git", "rev-parse", `${this.options.sha ?? BRANCH}^{commit}`]);
    if (!this.succeeds(["git", "merge-base", "--is-ancestor", sha, BRANCH])) {
      throw new BuildError(`${sha} is not on GitHub's main. Push it first.`);
    }
    if (this.capture(["git", "tag", "--list", tag])) throw new BuildError(`${tag} already exists`);
    const previous = this.capture(["git", "tag", "--list", "v*.*.*", "--sort=-v:refname"]).split("\n")[0] || undefined;
    if (previous && !newer(version, previous)) throw new BuildError(`${version} must be newer than ${previous}`);
    const identity = this.identity();
    this.capture(["xcrun", "notarytool", "history", "--keychain-profile", NOTARY_PROFILE], REPO, NETWORK_TIMEOUT);
    this.capture(["gh", "auth", "status"]);

    if (this.capture(["git", "status", "--porcelain"], worktree)) throw new BuildError(`${worktree} has local changes`);
    this.step(["git", "checkout", "--detach", sha], worktree);
    this.step(["pnpm", "install", "--frozen-lockfile", "--ignore-scripts"], worktree);
    this.step(["node", "scripts/chromium.ts", "build", "--jobs", String(jobs)], worktree);
    this.step(["node", "scripts/chromium.ts", "smoke"], worktree);
    this.step([join(this.tools, "autoninja"), "-C", "out/uw", `-j${jobs}`, "chrome/installer/mac"], this.source);

    const output = join(this.source, "out/uw");
    const directory = join(worktree, ".chromium/releases", version);
    const input = join(directory, "input");
    const app = join(input, "uw.app");
    rmSync(directory, { recursive: true, force: true });
    mkdirSync(input, { recursive: true });
    this.step(["cp", "-cR", join(output, "uw.app"), join(output, "uw Packaging"), input], directory);
    for (const key of ["CFBundleShortVersionString", "CFBundleVersion"]) {
      this.step(["plutil", "-replace", key, "-string", version, join(app, "Contents/Info.plist")], directory);
    }
    this.step([
      "codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp",
      "--identifier", "com.uwbrowser.uw.command", "--entitlements", join(worktree, "command/entitlements.plist"),
      join(app, "Contents/Helpers/uw"),
    ], directory);
    this.step([
      "python3", join(input, "uw Packaging/sign_chrome.py"), "--input", input, "--output", join(directory, "signed"),
      "--identity", identity, "--notarize", "staple", "--notary-arg=--keychain-profile", `--notary-arg=${NOTARY_PROFILE}`,
    ], directory);

    const signed = join(directory, "signed/stable");
    const packaged = readdirSync(signed).filter((name) => name.endsWith(".dmg"));
    if (packaged.length !== 1) throw new BuildError(`Expected one DMG in ${signed}, found ${packaged.length}`);
    const dmg = join(directory, DMG);
    renameSync(join(signed, packaged[0]!), dmg);
    this.step(["codesign", "--verify", "--deep", "--strict", join(signed, "uw.app")], directory);
    this.step(["xcrun", "stapler", "validate", dmg], directory);

    const chromium = JSON.parse(readFileSync(join(worktree, "chromium/pins.json"), "utf8")).version as string;
    const subjects = previous ? this.capture(["git", "log", "--format=%s", `${previous}..${sha}`]).split("\n") : undefined;
    const notes = join(directory, "notes.md");
    writeFileSync(notes, draftNotes(version, chromium, subjects));
    const url = this.capture([
      "gh", "release", "create", tag, dmg, "--draft", "--title", `uw ${version} preview`,
      "--notes-file", notes, "--target", sha,
    ], REPO, NETWORK_TIMEOUT);
    this.runtime.log(`Drafted ${url}\nReview the notes there, then publish the release.`);
    return url;
  }
}

const HELP = `Build, sign, and notarize a uw preview, then publish it as a draft GitHub release.

Usage: node scripts/release.ts <version> [options]
  --sha SHA         Commit to release; default: GitHub's main. It must be pushed.
  --worktree PATH   Release worktree with its own .chromium; default: ~/dev/uw-release
  --jobs NUMBER     Parallel build jobs; default: 8
  --help, -h        Show this help

Example:
  node scripts/release.ts 0.1.0
`;

export function main(argv = process.argv.slice(2)): number {
  try {
    const { values, positionals } = parseArgs({
      args: argv, strict: true, allowPositionals: true,
      options: {
        sha: { type: "string" }, worktree: { type: "string" }, jobs: { type: "string" },
        help: { type: "boolean", short: "h" },
      },
    });
    if (values.help || !positionals.length) { console.log(HELP); return values.help ? 0 : 2; }
    if (positionals.length > 1) throw new BuildError("Give one version", 2);
    const jobs = Number(values.jobs ?? "8");
    if (!Number.isSafeInteger(jobs) || jobs < 1) throw new BuildError("--jobs must be a positive integer", 2);
    const worktree = values.worktree ?? join(homedir(), "dev/uw-release");
    if (!existsSync(join(worktree, ".chromium/src"))) throw new BuildError(`${worktree} has no .chromium checkout`, 2);
    new Release({ version: positionals[0]!, sha: values.sha, worktree, jobs }).publish();
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    return error instanceof BuildError ? error.exitCode : 1;
  }
}

if (import.meta.main) process.exitCode = main();
