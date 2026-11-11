#!/usr/bin/env node
import { createHash, createPublicKey, verify } from "node:crypto";
import { chmodSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { delimiter, join } from "node:path";
import { parseArgs } from "node:util";
import { BuildError, OUTPUT, REPO, nativeRuntime } from "./chromium.ts";
import type { Runtime } from "./chromium.ts";

export const TEAM_ID = "24MD6K3S56";
export const NOTARY_PROFILE = "uw-notary";
export const DMG = "uw-macos-arm64.dmg";
export const SPARKLE_VERSION = "2.10.1";
export const SPARKLE_SHA256 = "1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27";
export const SPARKLE_PUBLIC_KEY = "tUSDZsx93uVKuL7o7d/RMObl4n5XFcTn/G5SHNENRZk=";
// Seconds between Sparkle's checks: 5 hours, as Chrome does. See docs/plans/distribution.md.
export const UPDATE_CHECK_EVERY = 5 * 60 * 60;
export const SENTRY_CLI_VERSION = "3.8.2";
export const SENTRY_CLI_SHA256 = "c80d92b6d34eb64fe7402f56cc7968fbb15aab1c763254ad428b55cc98ec7dcf";
// Crash reports go to the Sentry project eslee/uw, and uw's pages report problems
// to PostHog through its managed proxy on uwbrowser.com. Both addresses are public
// in every release. See docs/plans/distribution.md, "Problem reports".
export const SENTRY_ORG = "eslee";
export const SENTRY_PROJECT = "uw";
export const CRASH_REPORT_URL =
  "https://o4512323831070720.ingest.us.sentry.io/api/4512323876618240/minidump/?sentry_key=47ca5329021071c1a1fd038b8a1ec13e";
export const POSTHOG_HOST = "https://e.uwbrowser.com";
const REPOSITORY = "https://github.com/leetdavid/uw";
export const FEED_URL = `${REPOSITORY}/releases/latest/download/appcast.xml`;
// Each release also carries Windows uw, built on a Linux host in a release
// checkout of its own: a clone of uw with its own .chromium. Windows previews
// read only the latest release. See "Distribution" in docs/plans/windows.md.
export const WINDOWS_HOST = "workstation";
export const WINDOWS_CHECKOUT = "uw-windows-release";
export const WINDOWS_INSTALLER = "uw-windows-x64-setup.exe";
export const WINDOWS_FEED = "uw-windows-update.json";
export const WINDOWS_FEED_URL = "https://api.github.com/repos/leetdavid/uw/releases/latest";
// What scripts/windows-release.ts build leaves in the release's folder there.
export const WINDOWS_BUILT = "windows.json";
export const WINDOWS_FAILED = "windows-failed.txt";
// Shells that ssh starts there get Node and pnpm only from fnm and Homebrew,
// and the Windows toolchain's variables only from ~/win-toolchain/env.sh.
const WINDOWS_SHELL = 'eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)" && eval "$(fnm env --shell bash)" && ' +
  `source ~/win-toolchain/env.sh && cd ~/${WINDOWS_CHECKOUT} && fnm use --log-level quiet`;
const WINDOWS_JOBS = 16;
const WINDOWS_WAIT = 8 * 60 * 60_000;
const BRANCH = "origin/main";
const NETWORK_TIMEOUT = 300_000;
const LSREGISTER =
  "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";

export interface ReleaseOptions {
  version: string;
  sha?: string;
  worktree: string;
  jobs: number;
  // The git-ignored file the release's secrets come from; default: the repo's .env.
  envFile?: string;
  // The public key Windows uw checks its update feed with; default: Sparkle's.
  updateKey?: string;
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

/** PostHog's project token. */
export function posthogKey(env: NodeJS.ProcessEnv, envFile?: string): string {
  const key = envValue(env, "POSTHOG_PROJECT_TOKEN", envFile);
  if (!/^phc_\w+$/.test(key)) throw new BuildError("Set POSTHOG_PROJECT_TOKEN to PostHog's project token (phc_...) in .env");
  return key;
}

export function newer(version: string, than: string): boolean {
  const [a, b] = [version, than].map((value) => value.replace(/^v/, "").split(".").map(Number));
  for (let index = 0; index < 3; index++) {
    if (a![index] !== b![index]) return a![index]! > b![index]!;
  }
  return false;
}

/**
 * The next Windows build's four-part version on Chromium `chromium`, after the
 * newest Windows release, `previous`: Chromium's last part times 100 plus a
 * count. See docs/adr/0027-windows-uw-numbers-its-releases-from-chromiums-version.md.
 */
export function windowsVersion(chromium: string, previous?: string): string {
  const parts = chromium.split(".");
  const first = Number(parts[3]) * 100;
  const last = previous?.split(".");
  const same = last?.slice(0, 3).join(".") === parts.slice(0, 3).join(".") && Math.floor(Number(last?.[3]) / 100) === Number(parts[3]);
  const next = same ? Number(last![3]) + 1 : first;
  if (next >= first + 100) throw new BuildError(`Chromium ${chromium} has no room left for another Windows release`);
  return [...parts.slice(0, 3), next].join(".");
}

/** Whether `signature` is the Sparkle key's signature of `data`, as Windows uw checks its update feed. */
export function signedByUpdateKey(data: Buffer, signature: string, key = SPARKLE_PUBLIC_KEY): boolean {
  const spki = Buffer.concat([Buffer.from("302a300506032b6570032100", "hex"), Buffer.from(key, "base64")]);
  return verify(null, data, createPublicKey({ key: spki, format: "der", type: "spki" }), Buffer.from(signature.trim(), "base64"));
}

export function draftNotes(version: string, chromium: string, subjects: readonly string[] | undefined): string {
  const changes = subjects === undefined
    ? "The first public preview.\n"
    : subjects.filter((subject) => /^(feat|fix)(\(|:)/.test(subject)).map((subject) => `- ${subject}\n`).join("");
  return `uw ${version} is an early preview for Macs with Apple silicon on macOS 13 or later and for 64-bit PCs on Windows 11, ` +
    `built on Chromium ${chromium}. It may break, and it comes with no support promise.\n\n` +
    "The Windows installer isn't signed yet. On its first run Windows says \"Windows protected your PC\": choose **More info**, " +
    "then **Run anyway**. Smart App Control blocks it.\n\n## Changes\n\n" + changes;
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

  /** Runs `command` in the release checkout on the Windows build host. */
  private remote(command: string, { timeout = NETWORK_TIMEOUT, input, inherit }: { timeout?: number; input?: string; inherit?: boolean } = {}): string {
    return this.runtime.run(["ssh", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=60", WINDOWS_HOST, `${WINDOWS_SHELL} && ${command}`],
      { cwd: REPO, env: this.environment(), timeout, input, inherit });
  }

  /** The newest Windows release's four-part version, from its update feed. */
  previousWindowsVersion(): string | undefined {
    const tag = this.capture(["gh", "api", "repos/leetdavid/uw/releases?per_page=30", "--jq",
      `[.[] | select(any(.assets[]; .name == "${WINDOWS_FEED}")) | .tag_name][0] // ""`], REPO, NETWORK_TIMEOUT);
    if (!tag) return undefined;
    const feed = this.capture(["gh", "release", "download", tag, "--pattern", WINDOWS_FEED, "--output", "-"], REPO, NETWORK_TIMEOUT);
    return (JSON.parse(feed) as { version: string }).version;
  }

  /** Starts the Windows build on its host, where it runs while the Mac builds. */
  private startWindows(sha: string, windows: string, posthogToken: string): void {
    const folder = `.chromium/releases/${this.options.version}`;
    if (this.remote("git status --porcelain")) throw new BuildError(`${WINDOWS_HOST}:~/${WINDOWS_CHECKOUT} has local changes`);
    if (this.remote("pgrep -f '[w]indows-release.ts build' || true")) {
      throw new BuildError(`A Windows release build is still running on ${WINDOWS_HOST}`);
    }
    this.remote(`git fetch -q origin && git checkout -q --detach ${sha} && pnpm install --frozen-lockfile --ignore-scripts && ` +
      `mkdir -p ${folder} && (setsid nohup node scripts/windows-release.ts build ${this.options.version} ` +
      `--patch ${windows.split(".")[3]} --posthog-key ${posthogToken} --jobs ${WINDOWS_JOBS} > ${folder}/windows-build.log 2>&1 < /dev/null &)`);
    this.runtime.log(`Building Windows uw ${windows} on ${WINDOWS_HOST}. Log: ~/${WINDOWS_CHECKOUT}/${folder}/windows-build.log`);
  }

  /** Waits for the Windows build, uploads its symbols, checks it in the VM, and signs its update feed. */
  private finishWindows(directory: string, sentryToken: string, sparkle: string): string[] {
    const { version } = this.options;
    const folder = `.chromium/releases/${version}`;
    const built = JSON.parse(this.remote(
      `until [ -e ${folder}/${WINDOWS_BUILT} ] || [ -e ${folder}/${WINDOWS_FAILED} ]; do sleep 30; done; ` +
      `cat ${folder}/${WINDOWS_BUILT} 2>/dev/null || { cat ${folder}/${WINDOWS_FAILED} >&2; exit 1; }`,
      { timeout: WINDOWS_WAIT })) as { version: string; size: number; sha256: string };
    this.remote(`node scripts/windows-release.ts symbols ${version}`, { timeout: 2 * 60 * 60_000, input: sentryToken });
    this.remote(`node scripts/windows-release.ts check ${version}`, { timeout: 60 * 60_000, inherit: true });
    const windows = join(directory, "windows");
    mkdirSync(windows);
    const installer = join(windows, WINDOWS_INSTALLER);
    this.capture(["scp", "-q", `${WINDOWS_HOST}:${WINDOWS_CHECKOUT}/${folder}/${WINDOWS_INSTALLER}`, installer], REPO, NETWORK_TIMEOUT);
    if (createHash("sha256").update(readFileSync(installer)).digest("hex") !== built.sha256) {
      throw new BuildError(`The Windows installer copied from ${WINDOWS_HOST} does not match its build`);
    }
    const feed = join(windows, WINDOWS_FEED);
    writeFileSync(feed, JSON.stringify({ version: built.version, installer: WINDOWS_INSTALLER, size: built.size, sha256: built.sha256 }));
    const signature = this.capture([join(sparkle, "bin/sign_update"), "-p", feed]);
    if (!signedByUpdateKey(readFileSync(feed), signature, this.options.updateKey)) {
      throw new BuildError("sign_update's signature of the Windows update feed does not match uw's update key");
    }
    writeFileSync(`${feed}.sig`, `${signature}\n`);
    return [installer, feed, `${feed}.sig`];
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
    const posthogToken = posthogKey(this.runtime.env, this.options.envFile);
    const sentryToken = this.sentryToken();
    const cli = this.sentryCli();
    const chromium = (JSON.parse(this.capture(["git", "show", `${sha}:chromium/pins.json`])) as { version: string }).version;
    const windows = windowsVersion(chromium, this.previousWindowsVersion());

    if (this.capture(["git", "status", "--porcelain"], worktree)) throw new BuildError(`${worktree} has local changes`);
    this.startWindows(sha, windows, posthogToken);
    this.step(["git", "checkout", "--detach", sha], worktree);
    this.step(["pnpm", "install", "--frozen-lockfile", "--ignore-scripts"], worktree);
    this.step(["node", "scripts/chromium.ts", "build", "--jobs", String(jobs), "--symbols", "--release"], worktree);
    this.step(["node", "scripts/chromium.ts", "smoke"], worktree);
    this.step(["node", "scripts/google-check.ts"], worktree);
    this.step([join(this.tools, "autoninja"), "-C", OUTPUT, `-j${jobs}`, "chrome/installer/mac"], this.source);

    const output = join(this.source, OUTPUT);
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
    // The unsigned build has the installed uw's app ID. macOS could tie a
    // permission to it or open links in it, so make macOS forget it.
    this.step([LSREGISTER, "-u", join(output, "uw.app")], directory);
    const plist = join(app, "Contents/Info.plist");
    for (const [key, type, value] of [
      ["CFBundleShortVersionString", "-string", version], ["CFBundleVersion", "-string", version],
      ["SUFeedURL", "-string", FEED_URL], ["SUPublicEDKey", "-string", SPARKLE_PUBLIC_KEY],
      ["SUEnableAutomaticChecks", "-bool", "YES"], ["SUAutomaticallyUpdate", "-bool", "YES"],
      ["SUScheduledCheckInterval", "-integer", String(UPDATE_CHECK_EVERY)],
      ["UWCrashReportURL", "-string", CRASH_REPORT_URL], ["UWPostHogKey", "-string", posthogToken],
      ["UWPostHogHost", "-string", POSTHOG_HOST],
    ] as const) {
      this.step(["plutil", "-replace", key, type, value, plist], directory);
    }

    const sparkle = this.sparkle();
    const framework = join(app, "Contents/Frameworks/Sparkle.framework");
    this.step(["cp", "-cR", join(sparkle, "Sparkle.framework"), join(app, "Contents/Frameworks")], directory);
    // `rm` takes the top-level link too; a disk image's copy fails on a link to nothing.
    this.step(["rm", "-rf", join(framework, "Versions/B/XPCServices"), join(framework, "XPCServices")], directory);
    for (const part of ["Versions/B/Autoupdate", "Versions/B/Updater.app", ""]) {
      this.step(["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp", join(framework, part)],
        directory);
    }
    this.step([
      "codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp",
      "--identifier", "com.uwbrowser.uw.command", "--entitlements", join(worktree, "command/entitlements.plist"),
      join(app, "Contents/Helpers/uw"),
    ], directory);
    // Only this helper may load another team's library: Google's TranslateKit,
    // for translating on the device. See
    // docs/adr/0034-the-on-device-translation-engine-runs-in-its-own-helper.md.
    this.step([
      "codesign", "--force", "--sign", identity, "--options", "restrict,kill,runtime", "--timestamp",
      "--entitlements", join(worktree, "browser/translation/helper-translate-entitlements.plist"),
      join(app, "Contents/Frameworks/uw Framework.framework/Helpers/uw Helper (Translate).app"),
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

    const windowsFiles = this.finishWindows(directory, sentryToken, sparkle);
    const subjects = previous ? this.capture(["git", "log", "--format=%s", `${previous}..${sha}`]).split("\n") : undefined;
    const notes = join(directory, "notes.md");
    writeFileSync(notes, draftNotes(version, chromium, subjects));
    const url = this.capture([
      "gh", "release", "create", tag, dmg, appcast, ...windowsFiles, "--draft", "--title", `uw ${version} preview`,
      "--notes-file", notes, "--target", sha,
    ], REPO, NETWORK_TIMEOUT);
    this.runtime.log(`Drafted ${url}\nReview the notes there, then publish the release.`);
    return url;
  }
}

const HELP = `Build, sign, and notarize a uw preview for the Mac, have the Linux host "${WINDOWS_HOST}" build and check
it for Windows, then publish both as a draft GitHub release.

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
