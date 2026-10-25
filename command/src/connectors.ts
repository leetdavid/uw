/** uw connectors, also uw mcp: add, list, remove, and sign in to connectors. */

import { options, type Command, type Context, type Input, type Plan } from "./cli.ts";
import { CommandError, DONE, FAILED, NEEDS_YOU, USAGE } from "./errors.ts";

export const CONNECTORS_HELP = `uw connectors: add, list, remove, and sign in to connectors, the MCP servers
that give uw's agent tools. uw mcp is the same command.

Usage:
  uw mcp add [--transport http] <name> <url> [-H "Name: value"]...
  uw mcp add <name> [-e KEY[=value]]... -- <command> [args...]
  uw mcp add-json '<json>'            the JSON a server's README gives, or on stdin
  uw mcp add-json <name> '<json>'     one server, as claude mcp add-json takes it
  uw mcp list [--json]
  uw mcp remove <name>
  uw mcp sign-in <name> [--workspace <name>]

add takes the arguments of Claude Code's claude mcp add, so a README's line
works with claude swapped for uw. -e KEY with no value reads KEY from your
environment, so the key stays out of your shell history. Keys go to Keychain.
--scope is ignored: uw keeps one list for every profile. A web connector that
asks for a sign-in opens its sign-in page in uw; with no terminal, add leaves
the sign-in to uw mcp sign-in and exits with 3.

Examples:
  uw mcp add --transport http linear https://mcp.linear.app/mcp
  uw mcp add files -e FILES_TOKEN -- npx -y @acme/files-mcp
  uw mcp add-json '{"mcpServers": {"exa": {"type": "http", "url": "https://mcp.exa.ai/mcp"}}}'
  uw mcp sign-in linear
  uw mcp list --json
`;

export interface Connector {
  name: string;
  kind: "web" | "local";
  address: string;
  state: "off" | "starting" | "ready" | "needs-sign-in" | "signing-in" | "cant-sign-in" | "failed";
  error: string;
  tools: number;
  secrets: string[];
}

interface Listed {
  connectors: Connector[];
}

export const CONNECTORS: Command = { help: CONNECTORS_HELP, plan: connectorsPlan };

async function connectorsPlan(args: string[], input: Input): Promise<Plan | null> {
  const [verb, ...rest] = args;
  if (verb === undefined || verb === "--help" || verb === "-h") {
    if (verb === undefined) throw new CommandError("uw mcp needs add, add-json, list, remove, or sign-in.\n  uw mcp list\nRun uw mcp --help for more.", USAGE);
    return null;
  }
  if (verb === "add") return addPlan(rest, input);
  if (verb === "add-json") return addJsonPlan(rest, input);
  const { values, positionals } = options(rest, { workspace: { type: "string" } });
  if (values.help) return null;
  const [name, ...extra] = positionals;
  if (verb === "list" && name === undefined) {
    return { json: values.json, run: async ({ channel, out }) => {
      const listed = await channel.request("connectors.list") as Listed;
      out.data(listed, () => table(listed.connectors));
      return DONE;
    } };
  }
  if (verb === "remove" && name !== undefined && extra.length === 0) {
    return { json: values.json, run: async ({ channel, out }) => {
      const removed = await channel.request("connectors.remove", { name });
      out.data(removed, () => `Removed ${name}.\n`);
      return DONE;
    } };
  }
  if (verb === "sign-in" && name !== undefined && extra.length === 0) {
    const where = values.workspace === undefined ? {} : { workspace: values.workspace };
    return { json: values.json, run: async (context) => {
      const signedIn = await signIn(context, name, where);
      if (values.json) context.out.data({ connectors: [signedIn] }, () => "");
      return DONE;
    } };
  }
  throw new CommandError(misuse(verb), USAGE);
}

function misuse(verb: string): string {
  if (verb === "remove") return "uw mcp remove takes one connector.\n  uw mcp remove linear";
  if (verb === "sign-in") return "uw mcp sign-in takes one connector.\n  uw mcp sign-in linear";
  if (verb === "list") return "uw mcp list takes no connector.\n  uw mcp list";
  return `uw mcp has no ${verb}. It takes add, add-json, list, remove, or sign-in.\nRun uw mcp --help for more.`;
}

const ADD_OPTIONS = {
  transport: { type: "string", short: "t" },
  env: { type: "string", short: "e", multiple: true },
  header: { type: "string", short: "H", multiple: true },
  scope: { type: "string", short: "s" },
  workspace: { type: "string" },
} as const;

function addPlan(args: string[], input: Input): Plan | null {
  const { values, positionals } = options(args, ADD_OPTIONS);
  if (values.help) return null;
  const [name, target, ...rest] = positionals;
  const transport = values.transport ?? "stdio";
  if (!["stdio", "http", "sse"].includes(transport)) {
    throw new CommandError(`uw mcp add takes --transport stdio, http, or sse, not ${transport}.`, USAGE);
  }
  if (name === undefined || target === undefined) {
    throw new CommandError("uw mcp add takes a name and a command or an address.\n  uw mcp add --transport http linear https://mcp.linear.app/mcp\n  uw mcp add files -- npx -y @acme/files-mcp", USAGE);
  }
  const typed = (values.env ?? []).filter((entry) => entry.includes("="));
  const server = transport === "stdio"
    ? { command: target, args: rest, env: environment(values.env ?? [], input.env) }
    : { type: transport, url: target, headers: headers(values.header ?? []) };
  if (transport !== "stdio" && rest.length) {
    throw new CommandError(`A web connector takes one address, not ${rest.join(" ")}.`, USAGE);
  }
  const warnings = typed.map((entry) => {
    const key = entry.slice(0, entry.indexOf("="));
    return `${key}'s value is now in your shell history. Next time, pass -e ${key} to read it from your environment.`;
  });
  return addingPlan(JSON.stringify({ mcpServers: { [name]: server } }), values, warnings);
}

async function addJsonPlan(args: string[], input: Input): Promise<Plan | null> {
  const { values, positionals } = options(args, { workspace: { type: "string" } });
  if (values.help) return null;
  if (positionals.length > 2) throw new CommandError("uw mcp add-json takes a name and one JSON at most.", USAGE);
  const text = positionals.at(-1) ?? (input.terminal ? "" : await input.stdin());
  if (!text.trim()) {
    throw new CommandError("uw mcp add-json needs the JSON, as an argument or on stdin.\n  pbpaste | uw mcp add-json", USAGE);
  }
  const [name] = positionals.length === 2 ? positionals : [];
  if (name === undefined) return addingPlan(text, values, []);
  let server: unknown;
  try {
    server = JSON.parse(text);
  } catch {
    throw new CommandError("That isn't JSON. uw mcp add-json <name> takes one server's settings, such as {\"type\": \"http\", \"url\": \"https://mcp.exa.ai/mcp\"}.", USAGE);
  }
  return addingPlan(JSON.stringify({ mcpServers: { [name]: server } }), values, []);
}

function environment(entries: string[], env: NodeJS.ProcessEnv): Record<string, string> {
  const pairs = entries.map((entry): [string, string] => {
    const equals = entry.indexOf("=");
    if (equals > 0) return [entry.slice(0, equals), entry.slice(equals + 1)];
    const value = env[entry];
    if (value === undefined) {
      throw new CommandError(`${entry} isn't set in this shell. Set it first, or pass -e ${entry}=value.`, USAGE);
    }
    return [entry, value];
  });
  return Object.fromEntries(pairs);
}

function headers(entries: string[]): Record<string, string> {
  return Object.fromEntries(entries.map((entry): [string, string] => {
    const colon = entry.indexOf(":");
    if (colon <= 0) throw new CommandError(`A header looks like -H "Name: value", not ${entry}.`, USAGE);
    return [entry.slice(0, colon).trim(), entry.slice(colon + 1).trim()];
  }));
}

function addingPlan(json: string, values: { json: boolean; workspace?: string }, warnings: string[]): Plan {
  const where = values.workspace === undefined ? {} : { workspace: values.workspace };
  return { json: values.json, run: async (context) => {
    const { channel, out, terminal } = context;
    const added = await channel.request("connectors.add", { json }) as Listed;
    for (const warning of warnings) out.say(warning);
    let exit = DONE;
    const final: Connector[] = [];
    for (const connector of added.connectors) {
      out.line(addedLine(connector));
      if (connector.state === "needs-sign-in" && terminal) {
        final.push(await signIn(context, connector.name, where));
        continue;
      }
      if (connector.state === "needs-sign-in") {
        out.say(`${connector.name} needs a sign-in: run uw mcp sign-in ${connector.name}`);
        exit = Math.max(exit, NEEDS_YOU);
      }
      if (connector.state === "failed" || connector.state === "cant-sign-in") exit = Math.max(exit, FAILED);
      final.push(connector);
    }
    if (values.json) out.data({ connectors: final }, () => "");
    return exit;
  } };
}

async function signIn(context: Context, name: string, where: object): Promise<Connector> {
  context.out.say("Signing in. Finish in the tab that opened in uw.");
  const signedIn = await context.channel.request("connectors.signIn", { name, ...where }) as Listed;
  const connector = signedIn.connectors[0]!;
  context.out.line(`Signed in to ${name} · ${tools(connector.tools)}`);
  return connector;
}

function addedLine(connector: Connector): string {
  const kind = connector.kind === "web" ? "web" : "this Mac";
  const keys = connector.secrets.length
    ? ` ${connector.secrets.join(", ")} went to Keychain.`
    : "";
  const added = `Added ${connector.name} (${kind}).${keys}`;
  if (connector.state === "ready") return `${added} · ${tools(connector.tools)}`;
  if (connector.state === "failed") return `${added} It failed to start: ${connector.error}`;
  if (connector.state === "cant-sign-in") return `${added} ${connector.error}`;
  if (connector.state === "starting") return `${added} It's still starting; uw mcp list shows how it went.`;
  return added;
}

function tools(count: number): string {
  return count === 1 ? "1 tool" : `${count} tools`;
}

const STATE: Record<Connector["state"], string> = {
  "off": "Off",
  "starting": "Starting",
  "ready": "Running",
  "needs-sign-in": "Sign in",
  "signing-in": "Signing in",
  "cant-sign-in": "Can't sign in",
  "failed": "Failed",
};

function table(connectors: Connector[]): string {
  if (!connectors.length) return "No connectors yet.\n  uw mcp add --transport http linear https://mcp.linear.app/mcp\n";
  const rows = connectors.map((connector) => [
    connector.name,
    connector.kind === "web" ? "Web" : "This Mac",
    connector.state === "ready" && connector.kind === "web" ? "Signed in" : STATE[connector.state],
    connector.state === "ready" ? tools(connector.tools)
      : connector.state === "needs-sign-in" ? `run uw mcp sign-in ${connector.name}`
      : connector.error,
  ]);
  const widths = [0, 1, 2].map((column) => Math.max(...rows.map((row) => row[column]!.length)) + 2);
  return rows.map((row) => row.map((cell, column) => cell.padEnd(widths[column] ?? 0)).join("").trimEnd()).join("\n") + "\n";
}
