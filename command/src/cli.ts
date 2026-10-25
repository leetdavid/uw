/** Reads the command line, runs one command, and returns its exit code. */

import { parseArgs, type ParseArgsOptionsConfig } from "node:util";
import type { Channel } from "./channel.ts";
import { CONNECTORS } from "./connectors.ts";
import { CommandError, DONE, USAGE } from "./errors.ts";
import { VERSION, reach, type Place, type Reach } from "./launch.ts";
import { output, type Output, type Streams } from "./output.ts";
import { SETTINGS } from "./settings.ts";

export const HELP = `uw: run uw's agent, manage its connectors, and change uw's settings from a terminal.

Usage:
  uw <command> [options]

Commands:
  settings      List, read, and change uw's settings
  connectors    Add, list, remove, and sign in to connectors (also: uw mcp)

Options:
  --workspace <name>  The workspace a command applies to. Default: the one used last.
  --json              Print the result as JSON.
  --help              Show help for uw or one command.
  --version           Print this command's version.

Exit codes: 0 done, 1 failed or stopped, 2 wrong usage, 3 needs you.

Examples:
  uw settings list
  uw mcp add --transport http linear https://mcp.linear.app/mcp

Run uw <command> --help for a command's options.
`;

/** What a command needs once uw is reached. */
export interface Context {
  channel: Channel;
  out: Output;
  /** A person is at a terminal, so the command may wait for them. */
  terminal: boolean;
}

export interface Plan {
  json: boolean;
  run: (context: Context) => Promise<number>;
}

export interface Command {
  help: string;
  /** Checks the arguments before uw is reached, so wrong usage never starts uw. Null for --help. */
  plan: (args: string[], input: Input) => Promise<Plan | null>;
}

/** What a command may read besides its arguments. */
export interface Input {
  env: NodeJS.ProcessEnv;
  terminal: boolean;
  stdin: () => Promise<string>;
}

const COMMANDS: Record<string, Command> = { settings: SETTINGS, connectors: CONNECTORS, mcp: CONNECTORS };

/** Parses a command's options, with --json and --help, which every command takes. */
export function options<T extends ParseArgsOptionsConfig>(args: string[], own: T) {
  try {
    return parseArgs({
      args,
      allowPositionals: true,
      options: { ...own, json: { type: "boolean", default: false }, help: { type: "boolean", short: "h", default: false } },
    });
  } catch (error) {
    throw new CommandError(`${(error as Error).message}\nRun uw <command> --help for its options.`, USAGE);
  }
}

export interface Run extends Streams, Reach, Partial<Input> {
  place: Place;
}

export async function run(argv: string[], io: Run): Promise<number> {
  const [name, ...args] = argv;
  if (name === undefined || name === "--help" || name === "-h") {
    io.out(HELP);
    return name === undefined ? USAGE : DONE;
  }
  if (name === "--version") {
    io.out(`${VERSION}\n`);
    return DONE;
  }
  const command = Object.hasOwn(COMMANDS, name) ? COMMANDS[name] : undefined;
  if (!command) {
    io.err(`uw has no command called ${name}.\nRun uw --help for the commands.\n`);
    return USAGE;
  }
  const input: Input = { env: io.env ?? {}, terminal: io.terminal ?? false, stdin: io.stdin ?? (async () => "") };
  try {
    const plan = await command.plan(args, input);
    if (!plan) {
      io.out(command.help);
      return DONE;
    }
    const out = output(io, plan.json);
    const channel = await reach(io.place, { ...io, say: out.say });
    try {
      return await plan.run({ channel, out, terminal: input.terminal });
    } finally {
      channel.close();
    }
  } catch (error) {
    if (!(error instanceof CommandError)) throw error;
    io.err(`${error.message}\n`);
    return error.exit;
  }
}
