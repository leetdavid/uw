#!/usr/bin/env node
/**
 * Runs the built uw headless with a new profile, in a normal and a private
 * window, and fails when it contacts Google outside the requests uw keeps.
 * See "The Google check" in docs/plans/google-requests.md.
 */

import { spawn } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { createServer } from "node:http";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { parseArgs } from "node:util";
import { BuildError, REPO, layoutAt } from "./chromium.ts";

// The requests uw keeps on purpose, per "Kept" in docs/plans/google-requests.md.
export const KEPT: readonly { name: string; url: RegExp }[] = [
  { name: "component updater", url: /^https:\/\/update\.googleapis\.com\/service\/update2\// },
  { name: "component updater", url: /^https?:\/\/[\w.-]+\.gvt1\.com\/edgedl\// },
  { name: "component updater", url: /^https?:\/\/dl\.google\.com\// },
  { name: "component updater", url: /^https:\/\/clients2\.google(usercontent)?\.com\/(service\/update2|crx\/blobs)\// },
  { name: "push messaging", url: /^https:\/\/android\.clients\.google\.com\/(checkin|c2dm\/register3)$/ },
  { name: "sign-in check", url: /^https:\/\/accounts\.google\.com\/ListAccounts\?/ },
];

// Push messaging keeps a socket open to this host, outside any URL request.
export const KEPT_HOSTS: readonly string[] = ["mtalk.google.com"];

const GOOGLE = new RegExp(
  "(^|\\.)(google|googleapis|googleusercontent|googlevideo|google-analytics|googletagmanager|" +
  "googlesyndication|googleadservices|gstatic|gvt\\d|ggpht|doubleclick|withgoogle|youtube|ytimg|1e100)" +
  "\\.[a-z]{2,3}(\\.[a-z]{2})?$");

export const PAGE = `<!doctype html>
<title>uw Google check</title>
<form method="post" action="/">
  <input name="name" autocomplete="name">
  <input name="email" type="email" autocomplete="email">
  <input name="address" autocomplete="street-address">
  <input name="city" autocomplete="address-level2">
  <input name="postal" autocomplete="postal-code">
  <input name="password" type="password" autocomplete="new-password">
  <button>Sign up</button>
</form>
`;

const WINDOWS = { normal: [], private: ["--incognito"] } as const;
type Window = keyof typeof WINDOWS;

export interface Traffic {
  urls: string[];
  hosts: string[];
}

export function isGoogle(host: string): boolean {
  return GOOGLE.test(host.toLowerCase());
}

function hostOf(value: string): string {
  return value.replace(/^[a-z]+:\/\//, "").replace(/[:/].*$/, "").toLowerCase();
}

/** The URL requests and host lookups in a net log, also one cut off mid-write. */
export function readNetLog(text: string): Traffic {
  let log;
  try {
    log = JSON.parse(text);
  } catch {
    log = JSON.parse(`${text.replace(/,?\s*$/, "")}]}`);
  }
  const start = log.constants.logEventTypes.URL_REQUEST_START_JOB;
  const urls: string[] = [];
  const hosts = new Set<string>();
  for (const { type, params } of log.events ?? []) {
    if (type === start && typeof params?.url === "string") urls.push(params.url);
    if (typeof params?.host === "string") hosts.add(hostOf(params.host));
  }
  return { urls, hosts: [...hosts] };
}

function shortened(path: string): string {
  return path.replace(/[^/]{40,}/g, "…");
}

/** Google requests and hosts outside the kept list, without query strings or long payloads in the path. */
export function unexpected({ urls, hosts }: Traffic): string[] {
  const found = new Set<string>();
  const requested = new Set<string>();
  for (const url of urls) {
    if (!/^https?:/.test(url)) continue;
    const { hostname, origin, pathname } = new URL(url);
    requested.add(hostname);
    if (isGoogle(hostname) && !KEPT.some((kept) => kept.url.test(url))) found.add(origin + shortened(pathname));
  }
  for (const host of hosts) {
    if (isGoogle(host) && !requested.has(host) && !KEPT_HOSTS.includes(host)) found.add(host);
  }
  return [...found].sort();
}

async function serve(): Promise<Server> {
  const server = createServer((_, response) => {
    response.writeHead(200, { "content-type": "text/html" });
    response.end(PAGE);
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  return server;
}

function launch(binary: string, folder: string, window: Window, url: string): ChildProcess {
  mkdirSync(folder);
  return spawn(binary, [
    "--headless=new", `--user-data-dir=${join(folder, "profile")}`, "--use-mock-keychain", "--no-first-run",
    "--no-default-browser-check", "--mute-audio", `--log-net-log=${join(folder, "net.json")}`,
    ...WINDOWS[window], url,
  ], { stdio: "ignore" });
}

async function stop(browser: ChildProcess): Promise<void> {
  if (browser.exitCode !== null || browser.signalCode !== null) return;
  const exited = new Promise((resolve) => browser.once("exit", resolve));
  browser.kill("SIGTERM");
  await Promise.race([exited, sleep(15_000)]);
  if (browser.exitCode === null && browser.signalCode === null) {
    browser.kill("SIGKILL");
    await exited;
  }
}

/** Runs the check and reports what it found. True when uw kept to the list. */
export async function check(binary: string, seconds: number, log = console.log): Promise<boolean> {
  if (!existsSync(binary)) throw new BuildError(`${binary} is missing. Build uw first.`);
  const folder = mkdtempSync(join(tmpdir(), "uw-google-check-"));
  const server = await serve();
  const url = `http://127.0.0.1:${(server.address() as AddressInfo).port}/`;
  const windows = Object.keys(WINDOWS) as Window[];
  const browsers = windows.map((window) => launch(binary, join(folder, window), window, url));
  log(`Running uw for ${seconds} s in a normal and a private window...`);
  try {
    await sleep(seconds * 1000);
    const quit = windows.filter((_, index) => browsers[index]!.exitCode !== null);
    if (quit.length) throw new BuildError(`uw quit early in the ${quit.join(" and ")} window. Logs: ${folder}`);
  } finally {
    await Promise.all(browsers.map(stop));
    server.close();
  }
  const found = windows.flatMap((window) =>
    unexpected(readNetLog(readFileSync(join(folder, window, "net.json"), "utf8"))).map((what) => `${window.padEnd(7)}  ${what}`));
  if (!found.length) {
    rmSync(folder, { recursive: true, force: true });
    log("PASS: uw contacted Google only for the requests it keeps.");
    return true;
  }
  log(`FAIL: uw contacted Google outside the requests it keeps:\n${found.map((line) => `  ${line}`).join("\n")}`);
  log(`Network logs: ${folder}`);
  return false;
}

const HELP = `Run the built uw headless and fail when it contacts Google outside the requests it keeps.

Usage: pnpm run google-check [options]
  --seconds NUMBER      How long uw runs; default: 300. Background requests start 1 to 4 minutes in.
  --checkout-dir PATH   Chromium checkout; default: $UW_CHROMIUM_ROOT or .chromium
  --help, -h            Show this help

The kept requests are listed in docs/plans/google-requests.md, "Kept".
`;

export async function main(argv = process.argv.slice(2)): Promise<number> {
  try {
    const { values } = parseArgs({
      args: argv, strict: true, allowPositionals: false,
      options: { seconds: { type: "string" }, "checkout-dir": { type: "string" }, help: { type: "boolean", short: "h" } },
    });
    if (values.help) { console.log(HELP); return 0; }
    const seconds = Number(values.seconds ?? "300");
    if (!Number.isSafeInteger(seconds) || seconds < 1) throw new BuildError("--seconds must be a positive integer", 2);
    const layout = layoutAt(values["checkout-dir"] ?? (process.env.UW_CHROMIUM_ROOT || join(REPO, ".chromium")));
    return await check(layout.binary, seconds) ? 0 : 1;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    if (error instanceof BuildError) return error.exitCode;
    return error instanceof Error && "code" in error && String(error.code).startsWith("ERR_PARSE_ARGS") ? 2 : 1;
  }
}

if (import.meta.main) process.exitCode = await main();
