import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { TestContext } from "node:test";
import { BuildError } from "../scripts/chromium.ts";
import type { CommandOptions, Runtime } from "../scripts/chromium.ts";
import {
  CRASH_REPORT_URL, DMG, FEED_URL, NOTARY_PROFILE, Release, SENTRY_CLI_SHA256, SPARKLE_PUBLIC_KEY,
  SPARKLE_SHA256, TEAM_ID, draftNotes, newer, posthogKeys,
} from "../scripts/release.ts";

const SHA = "c".repeat(40);
const HASH = "A".repeat(40);

interface Setup {
  previous?: string;
  pushed?: boolean;
  subjects?: string[];
  checksum?: string;
  cliChecksum?: string;
  token?: string;
}

function fixture(t: TestContext, {
  previous = "", pushed = true, subjects = [], checksum = SPARKLE_SHA256, cliChecksum = SENTRY_CLI_SHA256, token = "sntrys_test",
}: Setup = {}) {
  const worktree = mkdtempSync(join(tmpdir(), "uw-release-test-"));
  t.after(() => rmSync(worktree, { recursive: true, force: true }));
  mkdirSync(join(worktree, "chromium"), { recursive: true });
  writeFileSync(join(worktree, "chromium/pins.json"), JSON.stringify({ version: "153.0.8010.53" }));
  const calls: { command: readonly string[]; options: CommandOptions }[] = [];
  const run = (command: readonly string[], options: CommandOptions = {}): string => {
    calls.push({ command, options });
    const line = command.join(" ");
    if (line.startsWith("git rev-parse")) return SHA;
    if (line.startsWith("git merge-base") && !pushed) throw new BuildError("not an ancestor");
    if (line === "git tag --list v*.*.* --sort=-v:refname") return previous;
    if (line.startsWith("security find-identity")) {
      return `  1) ${HASH} "Developer ID Application: Test Person (${TEAM_ID})"\n     1 valid identities found`;
    }
    if (line.startsWith("git log")) return subjects.join("\n");
    if (command[0] === "curl") writeFileSync(command[command.indexOf("-o") + 1]!, "download");
    if (line.startsWith("shasum")) return `${command.at(-1)!.includes("sentry-cli") ? cliChecksum : checksum}  ${command.at(-1)}`;
    if (command[1]?.endsWith("sign_chrome.py")) {
      const output = command[command.indexOf("--output") + 1]!;
      mkdirSync(output, { recursive: true });
      writeFileSync(join(output, "uw-153.0.8010.53.dmg"), "dmg");
    }
    if (line.startsWith("gh release create")) return "https://github.com/leetdavid/uw/releases/tag/untagged-1";
    return "";
  };
  const runtime: Runtime = {
    run, start: () => {},
    env: { PATH: "/usr/bin", POSTHOG_PROJECT_TOKEN: "phc_test", POSTHOG_PROJECT_REGION: "US", SENTRY_TOKEN: token }, platform: "darwin", arch: "arm64", nodeVersion: "24.13.0",
    memoryBytes: 64 * 1024 ** 3, freeBytes: () => 0, addresses: () => [], log: () => {},
  };
  const release = (version = "0.1.0") =>
    new Release({ version, worktree, jobs: 8, envFile: join(worktree, "no.env") }, runtime);
  const find = (prefix: string) => calls.map(({ command }) => command).filter((command) => command.join(" ").startsWith(prefix));
  return { worktree, calls, release, find };
}

test("newer compares versions part by part", () => {
  assert.equal(newer("0.1.0", "v0.0.9"), true);
  assert.equal(newer("0.10.0", "v0.9.0"), true);
  assert.equal(newer("0.1.0", "v0.1.0"), false);
  assert.equal(newer("0.1.0", "v1.0.0"), false);
});

test("draft notes list only feat and fix commits", () => {
  assert.match(draftNotes("0.1.0", "153.0.8010.53", undefined), /The first public preview\./);
  const notes = draftNotes("0.2.0", "155.0.8059.85", ["feat(x): add a", "docs(y): explain b", "fix: repair c"]);
  assert.match(notes, /built on Chromium 155\.0\.8059\.85/);
  assert.match(notes, /- feat\(x\): add a\n- fix: repair c\n$/);
});

test("a release is built in the worktree, signed, notarized, and drafted on GitHub", (t) => {
  const { worktree, calls, release, find } = fixture(t);

  assert.equal(release().publish(), "https://github.com/leetdavid/uw/releases/tag/untagged-1");

  assert.deepEqual(find("git checkout"), [["git", "checkout", "--detach", SHA]]);
  assert.deepEqual(find("node scripts/chromium.ts").map((command) => command.slice(2)), [
    ["build", "--jobs", "8", "--symbols"], ["smoke"],
  ]);
  assert.deepEqual(find("node scripts/google-check.ts"), [["node", "scripts/google-check.ts"]]);
  assert.deepEqual(find("plutil").map((command) => command.slice(2, 5)), [
    ["CFBundleShortVersionString", "-string", "0.1.0"], ["CFBundleVersion", "-string", "0.1.0"],
    ["SUFeedURL", "-string", FEED_URL], ["SUPublicEDKey", "-string", SPARKLE_PUBLIC_KEY],
    ["SUEnableAutomaticChecks", "-bool", "YES"], ["SUAutomaticallyUpdate", "-bool", "YES"],
    ["UWCrashReportURL", "-string", CRASH_REPORT_URL], ["UWPostHogKey", "-string", "phc_test"],
    ["UWPostHogHost", "-string", "https://us.i.posthog.com"],
  ]);
  const cli = join(worktree, ".chromium/releases/sentry-cli-3.8.2");
  const upload = calls.find(({ command }) => command[0] === cli);
  assert.ok(upload);
  assert.deepEqual(upload.command.slice(1), [
    "debug-files", "upload", "--org", "eslee", "--project", "uw", "--type", "dsym", "--wait",
    join(worktree, ".chromium/src/out/uw"),
  ]);
  assert.equal(upload.options.env?.SENTRY_AUTH_TOKEN, "sntrys_test");
  const framework = (path: string) => path.split("Contents/Frameworks/")[1];
  const [removed] = find("rm -rf");
  assert.ok(removed);
  assert.deepEqual(removed.slice(2).map(framework), [
    "Sparkle.framework/Versions/B/XPCServices", "Sparkle.framework/XPCServices",
  ]);
  const signing = find("codesign --force");
  assert.deepEqual(signing.slice(0, 3).map((command) => framework(command.at(-1)!)), [
    "Sparkle.framework/Versions/B/Autoupdate", "Sparkle.framework/Versions/B/Updater.app", "Sparkle.framework",
  ]);
  const helper = signing.find((command) => command.includes("--identifier"));
  assert.ok(helper);
  assert.equal(helper[helper.indexOf("--sign") + 1], HASH);
  assert.equal(helper[helper.indexOf("--entitlements") + 1], join(worktree, "command/entitlements.plist"));
  assert.ok(helper.at(-1)!.endsWith("uw.app/Contents/Helpers/uw"));
  const [sign] = find("python3");
  assert.ok(sign);
  assert.deepEqual(sign.slice(sign.indexOf("--identity")), [
    "--identity", HASH, "--notarize", "staple", "--notary-arg=--keychain-profile", `--notary-arg=${NOTARY_PROFILE}`,
  ]);

  const directory = join(worktree, ".chromium/releases/0.1.0");
  const dmg = join(directory, "feed", DMG);
  assert.ok(existsSync(dmg));
  const [appcast] = find(join(directory, "../sparkle-2.10.1/bin/generate_appcast"));
  assert.ok(appcast);
  assert.deepEqual(appcast.slice(1), [
    "--download-url-prefix", "https://github.com/leetdavid/uw/releases/download/v0.1.0/",
    "--maximum-deltas", "0", "-o", join(directory, "appcast.xml"), join(directory, "feed"),
  ]);
  assert.match(readFileSync(join(directory, "notes.md"), "utf8"), /The first public preview\./);
  const [draft] = find("gh release create");
  assert.ok(draft);
  assert.deepEqual(draft.slice(0, 7), ["gh", "release", "create", "v0.1.0", dmg, join(directory, "appcast.xml"), "--draft"]);
  assert.equal(draft[draft.indexOf("--target") + 1], SHA);
});

test("a release needs the Sentry token before it builds", (t) => {
  const { release, find } = fixture(t, { token: "" });

  assert.throws(() => release().publish(), /Set SENTRY_TOKEN/);
  assert.deepEqual(find("node scripts/chromium.ts"), []);
});

test("a sentry-cli download that does not match its checksum is refused", (t) => {
  const { worktree, release, find } = fixture(t, { cliChecksum: "0".repeat(64) });

  assert.throws(() => release().publish(), /sentry-cli 3\.8\.2 does not match its pinned checksum/);
  assert.equal(existsSync(join(worktree, ".chromium/releases/sentry-cli-3.8.2")), false);
  assert.deepEqual(find("node scripts/chromium.ts"), []);
});

test("PostHog's token and region come from the environment or else .env", (t) => {
  const dir = mkdtempSync(join(tmpdir(), "uw-env-test-"));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const file = join(dir, ".env");
  writeFileSync(file, "ANTHROPIC_API_KEY=secret\nPOSTHOG_PROJECT_TOKEN=phc_file\nPOSTHOG_PROJECT_REGION=EU   \n");
  assert.deepEqual(posthogKeys({}, file), { key: "phc_file", host: "https://eu.i.posthog.com" });
  assert.deepEqual(posthogKeys({ POSTHOG_PROJECT_TOKEN: "phc_env" }, file), { key: "phc_env", host: "https://eu.i.posthog.com" });
  assert.throws(() => posthogKeys({}, join(dir, "missing")), /POSTHOG_PROJECT_TOKEN/);
  assert.throws(() => posthogKeys({ POSTHOG_PROJECT_TOKEN: "phc_x", POSTHOG_PROJECT_REGION: "asia" }, join(dir, "missing")), /US or EU/);
});

test("the notes list the changes since the previous release", (t) => {
  const { worktree, release, find } = fixture(t, { previous: "v0.1.0", subjects: ["fix(tabs): keep order"] });

  release("0.1.1").publish();

  assert.deepEqual(find("git log"), [["git", "log", "--format=%s", `v0.1.0..${SHA}`]]);
  assert.match(readFileSync(join(worktree, ".chromium/releases/0.1.1/notes.md"), "utf8"), /- fix\(tabs\): keep order/);
});

test("a Sparkle download that does not match its checksum is refused", (t) => {
  const { release, find } = fixture(t, { checksum: "0".repeat(64) });

  assert.throws(() => release().publish(), /Sparkle 2\.10\.1 does not match its pinned checksum/);
  assert.deepEqual(find("tar"), []);
  assert.deepEqual(find("python3"), []);
});

test("a commit that is not on GitHub's main is refused before building", (t) => {
  const { release, find } = fixture(t, { pushed: false });

  assert.throws(() => release().publish(), /is not on GitHub's main\. Push it first\./);
  assert.deepEqual(find("node scripts/chromium.ts"), []);
});

test("a version that is not newer than the last release is refused", (t) => {
  const { release, find } = fixture(t, { previous: "v0.2.0" });

  assert.throws(() => release("0.1.0").publish(), /0\.1\.0 must be newer than v0\.2\.0/);
  assert.throws(() => release("1.0").publish(), /must look like 0\.1\.0/);
  assert.deepEqual(find("git checkout"), []);
});
