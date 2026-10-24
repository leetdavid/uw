/** Reads the command line, runs one command, and returns its exit code. */

import { parseArgs } from "node:util";
import { CommandError, DONE, USAGE } from "./errors.ts";
import { VERSION, reach, type Place, type Reach } from "./launch.ts";
import { output, type Streams } from "./output.ts";
import type { Call } from "./call.ts";
import { SETTINGS_HELP, settingsCall } from "./settings.ts";

export const HELP = `uw: run uw's agent, manage its connectors, and change uw's settings from a terminal.

Usage:
  uw <command> [options]

Commands:
  settings    List, read, and change uw's settings

Options:
  --workspace <name>  The workspace a command applies to. Default: the one used last.
  --json              Print the result as JSON.
  --help              Show help for uw or one command.
  --version           Print this command's version.

Exit codes: 0 done, 1 failed or stopped, 2 wrong usage, 3 needs you.

Examples:
  uw settings list
  uw settings set organizer.enabled false --workspace Work

Run uw <command> --help for a command's options.
`;

interface Command {
  help: string;
  call: (args: string[], workspace: string | undefined) => Call;
}

const COMMANDS: Record<string, Command> = {
  settings: { help: SETTINGS_HELP, call: settingsCall },
};

export interface Run extends Streams, Reach {
  place: Place;
}

export async function run(argv: string[], io: Run): Promise<number> {
  let parsed;
  try {
    parsed = parseArgs({
      args: argv,
      allowPositionals: true,
      options: {
        workspace: { type: "string" },
        json: { type: "boolean", default: false },
        help: { type: "boolean", short: "h", default: false },
        version: { type: "boolean", default: false },
      },
    });
  } catch (error) {
    io.err(`${(error as Error).message}\nRun uw --help for the options.\n`);
    return USAGE;
  }
  const { values, positionals } = parsed;
  const [command, ...args] = positionals;
  if (values.version) {
    io.out(`${VERSION}\n`);
    return DONE;
  }
  if (command === undefined || (values.help && !Object.hasOwn(COMMANDS, command))) {
    io.out(HELP);
    return command === undefined && !values.help ? USAGE : DONE;
  }
  if (!Object.hasOwn(COMMANDS, command)) {
    io.err(`uw has no command called ${command}.\nRun uw --help for the commands.\n`);
    return USAGE;
  }
  if (values.help) {
    io.out(COMMANDS[command]!.help);
    return DONE;
  }
  const out = output(io, values.json);
  try {
    const call = COMMANDS[command]!.call(args, values.workspace);
    const channel = await reach(io.place, { ...io, say: out.say });
    try {
      const result = await channel.request(call.method, call.params);
      out.data(result, () => call.describe(result));
      return DONE;
    } finally {
      channel.close();
    }
  } catch (error) {
    if (!(error instanceof CommandError)) throw error;
    io.err(`${error.message}\n`);
    return error.exit;
  }
}
