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

const LINEAR = {
  name: "linear", kind: "web", address: "https://mcp.linear.app/mcp", state: "needs-sign-in",
  error: "", tools: 0, secrets: [],
};
const FILES = {
  name: "files", kind: "local", address: "npx -y @acme/files-mcp", state: "ready", error: "", tools: 6,
  secrets: ["FILES_TOKEN"],
};

function connectorsUw(): Promise<void> {
  return fakeUw((method, params) => {
    if (method === "connectors.add") {
      const pasted = JSON.parse(params.json as string) as { mcpServers: Record<string, { type?: string }> };
      const [name] = Object.keys(pasted.mcpServers);
      return { result: { connectors: [name === "linear" ? LINEAR : { ...FILES, name }] } };
    }
    if (method === "connectors.signIn") return { result: { connectors: [{ ...LINEAR, state: "ready", tools: 41 }] } };
    if (method === "connectors.list") return { result: { connectors: [LINEAR, FILES] } };
    if (method === "connectors.remove" && params.name === "linear") return { result: { name: "linear" } };
    if (method === "connectors.remove") return { error: { code: 2, message: `No connector is called ${params.name}.` } };
    return { error: { code: -32601, message: `uw has no method ${method}.` } };
  });
}

function pasted(index = -1) {
  const add = requests.filter((request) => request.method === "connectors.add").at(index);
  return JSON.parse(add?.params.json as string) as { mcpServers: Record<string, Record<string, unknown>> };
}

test("claude mcp add's arguments add a web connector, and with no terminal its sign-in waits for you", async () => {
  await connectorsUw();
  const { exit, out, err } = await uw(["mcp", "add", "--transport", "http", "linear", "https://mcp.linear.app/mcp",
    "-H", "Authorization: Bearer abc", "--scope", "user"]);
  assert.equal(exit, 3);
  assert.deepEqual(pasted().mcpServers.linear,
    { type: "http", url: "https://mcp.linear.app/mcp", headers: { Authorization: "Bearer abc" } });
  assert.equal(out, "Added linear (web).\n");
  assert.equal(err, "linear needs a sign-in: run uw mcp sign-in linear\n");
});

test("in a terminal, add goes straight to the sign-in", async () => {
  await connectorsUw();
  const { exit, out, err } = await uw(["mcp", "add", "-t", "http", "linear", "https://mcp.linear.app/mcp"], { terminal: true });
  assert.equal(exit, 0);
  assert.equal(requests.at(-1)?.method, "connectors.signIn");
  assert.equal(out, "Added linear (web).\nSigned in to linear · 41 tools\n");
  assert.equal(err, "Signing in. Finish in the tab that opened in uw.\n");
});

test("a local connector reads -e KEY from the environment, and its key goes to Keychain", async () => {
  await connectorsUw();
  const { exit, out } = await uw(["connectors", "add", "files", "-e", "FILES_TOKEN", "--", "npx", "-y", "@acme/files-mcp"],
    { env: { FILES_TOKEN: "sk-test" } });
  assert.equal(exit, 0);
  assert.deepEqual(pasted().mcpServers.files, { command: "npx", args: ["-y", "@acme/files-mcp"], env: { FILES_TOKEN: "sk-test" } });
  assert.equal(out, "Added files (this Mac). FILES_TOKEN went to Keychain. · 6 tools\n");
});

test("-e KEY=value works, with a warning about shell history", async () => {
  await connectorsUw();
  const { exit, err } = await uw(["mcp", "add", "stripe", "-e", "STRIPE_KEY=sk_live_1", "--", "npx", "-y", "@stripe/mcp"]);
  assert.equal(exit, 0);
  assert.deepEqual(pasted().mcpServers.stripe?.env, { STRIPE_KEY: "sk_live_1" });
  assert.match(err, /STRIPE_KEY's value is now in your shell history\. Next time, pass -e STRIPE_KEY/);
});

test("mistakes in add are wrong usage, before uw is reached", async () => {
  assert.match((await uw(["mcp", "add", "files", "-e", "MISSING", "--", "npx"], { env: {} })).err,
    /MISSING isn't set in this shell/);
  assert.equal((await uw(["mcp", "add", "-t", "ws", "x", "wss://x"])).exit, 2);
  assert.equal((await uw(["mcp", "add", "-t", "http", "x", "https://x", "-H", "NoColon"])).exit, 2);
  assert.equal((await uw(["mcp", "add", "linear"])).exit, 2);
  assert.equal((await uw(["mcp"])).exit, 2);
  assert.equal((await uw(["mcp", "nope"])).exit, 2);
  assert.deepEqual(requests, []);
});

test("add-json takes the paste box's JSON on stdin, or one server with its name", async () => {
  await connectorsUw();
  const json = '{"mcpServers": {"exa": {"type": "http", "url": "https://mcp.exa.ai/mcp"}}}';
  assert.equal((await uw(["mcp", "add-json"], { stdin: async () => json })).exit, 0);
  assert.equal(requests.at(-1)?.params.json, json);
  assert.equal((await uw(["mcp", "add-json", "exa", '{"type": "http", "url": "https://mcp.exa.ai/mcp"}'])).exit, 0);
  assert.deepEqual(pasted().mcpServers.exa, { type: "http", url: "https://mcp.exa.ai/mcp" });
  assert.equal((await uw(["mcp", "add-json", "exa", "{nope"])).exit, 2);
});

test("list shows each connector's state, and what to do about a sign-in", async () => {
  await connectorsUw();
  assert.equal((await uw(["mcp", "list"])).out, [
    "linear  Web       Sign in  run uw mcp sign-in linear",
    "files   This Mac  Running  6 tools",
    "",
  ].join("\n"));
  assert.deepEqual(JSON.parse((await uw(["mcp", "list", "--json"])).out).connectors[1], FILES);
});

test("remove says what went, and an unknown name is wrong usage", async () => {
  await connectorsUw();
  assert.equal((await uw(["mcp", "remove", "linear"])).out, "Removed linear.\n");
  const missing = await uw(["mcp", "remove", "nope"]);
  assert.equal(missing.exit, 2);
  assert.equal(missing.err, "No connector is called nope.\n");
});

test("mcp's help has its examples", async () => {
  const help = await uw(["mcp", "--help"]);
  assert.equal(help.exit, 0);
  assert.match(help.out, /Examples:\n {2}uw mcp add --transport http linear https:\/\/mcp\.linear\.app\/mcp/);
  assert.equal((await uw(["mcp", "add", "--help"])).out, help.out);
});
