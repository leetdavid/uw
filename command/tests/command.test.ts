import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { createServer, type Server } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";
import { afterEach, beforeEach, test } from "node:test";
import { run, type Run } from "../src/cli.ts";
import { NOT_INSTALLED, SOCKET, place, type Place } from "../src/launch.ts";

type Handler = (method: string, params: Record<string, unknown>) => { result?: unknown; error?: { code: number; message: string } };

const WORK = { name: "organizer.enabled", value: true, takes: "true, false", appliesTo: ["Work", "Side projects"] };
const GRAIN = { name: "theme.grain", value: 20, takes: "0 to 100", appliesTo: ["Work"] };

let dir: string;
let where: Place;
let servers: Server[];
let requests: { method: string; params: Record<string, unknown> }[];

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "uw-command-"));
  mkdirSync(join(dir, "uw.app", "Contents", "Helpers"), { recursive: true });
  writeFileSync(join(dir, "uw.app", "Contents", "Helpers", "uw"), "");
  where = { app: join(dir, "uw.app"), userData: join(dir, "data"), defaultUserData: false, link: join(dir, "link") };
  mkdirSync(where.userData);
  servers = [];
  requests = [];
});

afterEach(() => {
  for (const server of servers) server.close();
  rmSync(dir, { recursive: true, force: true });
});

function fakeUw(handle: Handler, version = "dev"): Promise<void> {
  const server = createServer((socket) => {
    createInterface({ input: socket }).on("line", (line) => {
      const { id, method, params } = JSON.parse(line) as { id: number; method: string; params: Record<string, unknown> };
      requests.push({ method, params });
      const reply = method === "hello" ? { result: { version } } : handle(method, params);
      socket.write(`${JSON.stringify({ jsonrpc: "2.0", id, ...reply })}\n`);
    });
  });
  servers.push(server);
  return new Promise((resolve) => server.listen(join(where.userData, SOCKET), resolve));
}

async function uw(argv: string[], extra: Partial<Run> = {}) {
  let out = "";
  let err = "";
  const exit = await run(argv, { out: (text) => { out += text; }, err: (text) => { err += text; }, place: where, ...extra });
  return { exit, out, err };
}

function settingsUw(): Promise<void> {
  return fakeUw((method, params) => {
    if (method === "settings.list") return { result: { settings: [WORK, GRAIN] } };
    if (method === "settings.get") return { result: params.name === "theme.grain" ? GRAIN : WORK };
    if (method === "settings.set" && params.value === "140") return { error: { code: 2, message: "theme.grain takes 0 to 100." } };
    if (method === "settings.set") return { result: params.name === "theme.grain" ? { ...GRAIN, value: 40 } : { ...WORK, value: false } };
    return { error: { code: -32601, message: `uw has no method ${method}.` } };
  });
}

test("settings list prints a table, and the workspace goes to uw", async () => {
  await settingsUw();
  const { exit, out } = await uw(["settings", "list", "--workspace", "Work"]);
  assert.equal(exit, 0);
  assert.deepEqual(requests.at(-1), { method: "settings.list", params: { workspace: "Work" } });
  assert.equal(out, [
    "Setting            Value  Takes        Applies to",
    "organizer.enabled  true   true, false  Work, Side projects",
    "theme.grain        20     0 to 100     Work",
    "",
  ].join("\n"));
});

test("--json prints uw's result as it came", async () => {
  await settingsUw();
  const { out } = await uw(["settings", "get", "theme.grain", "--json"]);
  assert.deepEqual(JSON.parse(out), GRAIN);
});

test("settings get prints the value alone, so a script can use it", async () => {
  await settingsUw();
  assert.equal((await uw(["settings", "get", "theme.grain"])).out, "20\n");
});

test("settings set says which workspaces the change reached", async () => {
  await settingsUw();
  assert.equal((await uw(["settings", "set", "organizer.enabled", "false"])).out,
    "organizer.enabled is now false for Work and Side projects (same profile).\n");
  assert.equal((await uw(["settings", "set", "theme.grain", "40"])).out, "theme.grain is now 40 for Work.\n");
  assert.deepEqual(requests.at(-1)?.params, { name: "theme.grain", value: "40" });
});

test("a value uw refuses exits with 2 and uw's reason", async () => {
  await settingsUw();
  const { exit, out, err } = await uw(["settings", "set", "theme.grain", "140"]);
  assert.equal(exit, 2);
  assert.equal(out, "");
  assert.equal(err, "theme.grain takes 0 to 100.\n");
});

test("wrong usage exits with 2 and an example, without reaching uw", async () => {
  const { exit, err } = await uw(["settings", "set", "theme.grain"]);
  assert.equal(exit, 2);
  assert.equal(err, "uw settings set takes a setting and a value.\n  uw settings set theme.grain 20\n");
  assert.equal((await uw(["settings"])).exit, 2);
  assert.equal((await uw(["settings", "list", "--bogus"])).exit, 2);
  assert.match((await uw(["nope"])).err, /uw has no command called nope/);
  assert.deepEqual(requests, []);
});

test("help comes in layers, each with examples", async () => {
  const bare = await uw([]);
  assert.equal(bare.exit, 2);
  assert.match(bare.out, /Commands:\n {2}settings/);
  const top = await uw(["--help"]);
  assert.equal(top.exit, 0);
  assert.match(top.out, /Examples:\n {2}uw settings list/);
  const settings = await uw(["settings", "--help"]);
  assert.match(settings.out, /^uw settings: /);
  assert.match(settings.out, /Examples:\n {2}uw settings list --workspace Work/);
});

test("without the link, the command says how to install it", async () => {
  const { exit, err } = await uw(["settings", "list"]);
  assert.equal(exit, 1);
  assert.equal(err, `${NOT_INSTALLED}\n`);
});

test("a running uw that isn't listening is not started again", async () => {
  symlinkSync(join(where.app!, "Contents", "Helpers", "uw"), where.link);
  symlinkSync(`host-${process.pid}`, join(where.userData, "SingletonLock"));
  let started = false;
  const { exit, err } = await uw(["settings", "list"], { start: () => { started = true; } });
  assert.equal(exit, 1);
  assert.match(err, /uw is running but isn't answering/);
  assert.equal(started, false);
});

test("an installed command starts uw with no window and waits for it", async () => {
  symlinkSync(join(where.app!, "Contents", "Helpers", "uw"), where.link);
  const { exit, out, err } = await uw(["settings", "get", "theme.grain"], { start: () => void settingsUw() });
  assert.equal(exit, 0);
  assert.equal(err, "Starting uw with no window…\n");
  assert.equal(out, "20\n");
});

test("a uw older than the command asks for a restart", async () => {
  await fakeUw(() => ({ result: {} }), "153.0.1");
  const { exit, err } = await uw(["settings", "list"], { version: "153.0.2" });
  assert.equal(exit, 1);
  assert.equal(err, "Restart uw to finish its update. uw 153.0.1 is running; this command is 153.0.2.\n");
});

test("the command finds its app from its own place in the bundle", () => {
  const binary = join(dir, "uw.app", "Contents", "Helpers", "uw");
  const found = place({ HOME: dir }, binary);
  assert.match(found.app ?? "", /\/uw\.app$/);
  assert.equal(found.defaultUserData, true);
  assert.equal(place({ UW_USER_DATA_DIR: where.userData }, binary).userData, where.userData);
  assert.equal(place({}, process.execPath).app, undefined);
});
