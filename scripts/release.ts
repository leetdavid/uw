#!/usr/bin/env node
import { chmodSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { delimiter, join } from "node:path";
import { parseArgs } from "node:util";
import { BuildError, REPO, nativeRuntime } from "./chromium.ts";
import type { Runtime } from "./chromium.ts";

export const TEAM_ID = "24MD6K3S56";
export const NOTARY_PROFILE = "uw-notary";
export const DMG = "uw-macos-arm64.dmg";
export const SPARKLE_VERSION = "2.10.1";
export const SPARKLE_SHA256 = "1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27";
export const SPARKLE_PUBLIC_KEY = "tUSDZsx93uVKuL7o7d/RMObl4n5XFcTn/G5SHNENRZk=";
export const SENTRY_CLI_VERSION = "3.8.2";
export const SENTRY_CLI_SHA256 = "c80d92b6d34eb64fe7402f56cc7968fbb15aab1c763254ad428b55cc98ec7dcf";
// Crash reports go to the Sentry project eslee/uw, and uw's pages report problems
// to PostHog. Both addresses are public in every release. See
// docs/plans/distribution.md, "Problem reports".
export const SENTRY_ORG = "eslee";
export const SENTRY_PROJECT = "uw";
export const CRASH_REPORT_URL =
  "https://o4512323831070720.ingest.us.sentry.io/api/4512323876618240/minidump/?sentry_key=47ca5329021071c1a1fd038b8a1ec13e";
const REPOSITORY = "https://github.com/leetdavid/uw";
export const FEED_URL = `${REPOSITORY}/releases/latest/download/appcast.xml`;
const BRANCH = "origin/main";
const NETWORK_TIMEOUT = 300_000;

export interface ReleaseOptions {
  version: string;
  sha?: string;
  worktree: string;
  jobs: number;
  // The git-ignored file the release's secrets come from; default: the repo's .env.
  envFile?: string;
}

/**
 * One of the release's secrets, from the environment or else the git-ignored
 * `envFile`, of which nothing else is read.
 */
export function envValue(env: NodeJS.ProcessEnv, name: string, envFile = join(REPO, ".env")): string {
  const set = env[name]?.trim();
  if (set) return set;
  if (!existsSync(envFile)) return "";
  for (const line of readFileSync(envFile, "utf8").split("\n")) {
    const match = /^\s*([A-Z0-9_]+)\s*=\s*["']?(.*?)["']?\s*$/.exec(line);
    if (match?.[1] === name) return match[2]!;
  }
  return "";
}

/** PostHog's project token and region. */
export function posthogKeys(env: NodeJS.ProcessEnv, envFile?: string): { key: string; host: string } {
  const key = envValue(env, "POSTHOG_PROJECT_TOKEN", envFile);
  const region = envValue(env, "POSTHOG_PROJECT_REGION", envFile).toLowerCase();
  if (!/^phc_\w+$/.test(key)) throw new BuildError("Set POSTHOG_PROJECT_TOKEN to PostHog's project token (phc_...) in .env");
  if (region !== "us" && region !== "eu") throw new BuildError("Set POSTHOG_PROJECT_REGION to US or EU in .env");
  return { key, host: `https://${region}.i.posthog.com` };
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

  /** Downloads the pinned Sparkle release once and returns where it is unpacked. */
  sparkle(): string {
    const directory = join(this.options.worktree, ".chromium/releases", `sparkle-${SPARKLE_VERSION}`);
    const archive = `${directory}.tar.xz`;
    if (existsSync(join(directory, "Sparkle.framework"))) return directory;
    this.capture(["curl", "-fsSL", "-o", archive,
      `https://github.com/sparkle-project/Sparkle/releases/download/${SPARKLE_VERSION}/Sparkle-${SPARKLE_VERSION}.tar.xz`],
    REPO, NETWORK_TIMEOUT);
    if (this.capture(["shasum", "-a", "256", archive]).split(/\s/)[0] !== SPARKLE_SHA256) {
      rmSync(archive, { force: true });
      throw new BuildError(`Sparkle ${SPARKLE_VERSION} does not match its pinned checksum`);
    }
    mkdirSync(directory, { recursive: true });
    this.capture(["tar", "-xJf", archive, "-C", directory]);
    return directory;
  }

  /** The Sentry organization token that uploads symbols. */
  sentryToken(): string {
    const token = envValue(this.runtime.env, "SENTRY_TOKEN", this.options.envFile);
    if (!token) throw new BuildError("Set SENTRY_TOKEN to a Sentry organization token in .env");
    return token;
  }

  /** Downloads the pinned sentry-cli once and returns its path. */
  sentryCli(): string {
    const cli = join(this.options.worktree, ".chromium/releases", `sentry-cli-${SENTRY_CLI_VERSION}`);
    if (existsSync(cli)) return cli;
    const download = `${cli}.download`;
    mkdirSync(join(this.options.worktree, ".chromium/releases"), { recursive: true });
    this.capture(["curl", "-fsSL", "-o", download,
      `https://github.com/getsentry/sentry-cli/releases/download/${SENTRY_CLI_VERSION}/sentry-cli-Darwin-arm64`],
    REPO, NETWORK_TIMEOUT);
    if (this.capture(["shasum", "-a", "256", download]).split(/\s/)[0] !== SENTRY_CLI_SHA256) {
      rmSync(download, { force: true });
      throw new BuildError(`sentry-cli ${SENTRY_CLI_VERSION} does not match its pinned checksum`);
    }
    chmodSync(download, 0o755);
    renameSync(download, cli);
    return cli;
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
    const posthog = posthogKeys(this.runtime.env, this.options.envFile);
    const sentryToken = this.sentryToken();
    const cli = this.sentryCli();

    if (this.capture(["git", "status", "--porcelain"], worktree)) throw new BuildError(`${worktree} has local changes`);
    this.step(["git", "checkout", "--detach", sha], worktree);
    this.step(["pnpm", "install", "--frozen-lockfile", "--ignore-scripts"], worktree);
    this.step(["node", "scripts/chromium.ts", "build", "--jobs", String(jobs), "--symbols"], worktree);
    this.step(["node", "scripts/chromium.ts", "smoke"], worktree);
    this.step([join(this.tools, "autoninja"), "-C", "out/uw", `-j${jobs}`, "chrome/installer/mac"], this.source);

    const output = join(this.source, "out/uw");
    // Sentry reads a crash report with the dSYMs of exactly this build.
    this.runtime.log(`[${output}] sentry-cli debug-files upload --type dsym`);
    this.runtime.run([cli, "debug-files", "upload", "--org", SENTRY_ORG, "--project", SENTRY_PROJECT, "--type", "dsym",
      "--wait", output], { cwd: output, env: { ...this.environment(), SENTRY_AUTH_TOKEN: sentryToken }, inherit: true });
    const directory = join(worktree, ".chromium/releases", version);
    const input = join(directory, "input");
    const app = join(input, "uw.app");
    rmSync(directory, { recursive: true, force: true });
    mkdirSync(input, { recursive: true });
    this.step(["cp", "-cR", join(output, "uw.app"), join(output, "uw Packaging"), input], directory);
    const plist = join(app, "Contents/Info.plist");
    for (const [key, type, value] of [
      ["CFBundleShortVersionString", "-string", version], ["CFBundleVersion", "-string", version],
      ["SUFeedURL", "-string", FEED_URL], ["SUPublicEDKey", "-string", SPARKLE_PUBLIC_KEY],
      ["SUEnableAutomaticChecks", "-bool", "YES"], ["SUAutomaticallyUpdate", "-bool", "YES"],
      ["UWCrashReportURL", "-string", CRASH_REPORT_URL], ["UWPostHogKey", "-string", posthog.key],
      ["UWPostHogHost", "-string", posthog.host],
    ] as const) {
      this.step(["plutil", "-replace", key, type, value, plist], directory);
    }

    const sparkle = this.sparkle();
    const framework = join(app, "Contents/Frameworks/Sparkle.framework");
    this.step(["cp", "-cR", join(sparkle, "Sparkle.framework"), join(app, "Contents/Frameworks")], directory);
    rmSync(join(framework, "Versions/B/XPCServices"), { recursive: true, force: true });
    rmSync(join(framework, "XPCServices"), { force: true });
    for (const part of ["Versions/B/Autoupdate", "Versions/B/Updater.app", ""]) {
      this.step(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", join(framework, part)],
        directory);
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

    const signed = join(directory, "signed");
    const packaged = readdirSync(signed).filter((name) => name.endsWith(".dmg"));
    if (packaged.length !== 1) throw new BuildError(`Expected one DMG in ${signed}, found ${packaged.length}`);
    const feed = join(directory, "feed");
    const dmg = join(feed, DMG);
    mkdirSync(feed);
    renameSync(join(signed, packaged[0]!), dmg);
    this.step(["xcrun", "stapler", "validate", dmg], directory);
    const appcast = join(directory, "appcast.xml");
    this.step([
      join(sparkle, "bin/generate_appcast"), "--download-url-prefix", `${REPOSITORY}/releases/download/${tag}/`,
      "--maximum-deltas", "0", "-o", appcast, feed,
    ], directory);

    const chromium = JSON.parse(readFileSync(join(worktree, "chromium/pins.json"), "utf8")).version as string;
    const subjects = previous ? this.capture(["git", "log", "--format=%s", `${previous}..${sha}`]).split("\n") : undefined;
    const notes = join(directory, "notes.md");
    writeFileSync(notes, draftNotes(version, chromium, subjects));
    const url = this.capture([
      "gh", "release", "create", tag, dmg, appcast, "--draft", "--title", `uw ${version} preview`,
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
