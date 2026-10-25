/** uw settings: list, read, and change uw's own settings. */

import { options, type Command, type Plan } from "./cli.ts";
import { CommandError, DONE, USAGE } from "./errors.ts";

export const SETTINGS_HELP = `uw settings: list, read, and change uw's own settings.

Usage:
  uw settings list [--workspace <name>] [--json]
  uw settings get <setting> [--workspace <name>] [--json]
  uw settings set <setting> <value> [--workspace <name>] [--json]

A theme setting belongs to one workspace. Every other setting is shared by the
workspaces of one profile; list says which.

Examples:
  uw settings list --workspace Work
  uw settings get agent.approval-mode
  uw settings set organizer.enabled false
  uw settings set theme.grain 20 --workspace Personal
`;

export interface Setting {
  name: string;
  value: boolean | number | string;
  takes: string;
  appliesTo: string[];
}

export const SETTINGS: Command = { help: SETTINGS_HELP, plan: async (args) => settingsPlan(args) };

/** Checks the arguments before uw is reached, so wrong usage never starts uw. */
function settingsPlan(args: string[]): Plan | null {
  const { values, positionals } = options(args, { workspace: { type: "string" } });
  if (values.help) return null;
  const [verb, name, value, ...extra] = positionals;
  const where = values.workspace === undefined ? {} : { workspace: values.workspace };
  const request = (method: string, params: object, describe: (result: unknown) => string): Plan => ({
    json: values.json,
    run: async ({ channel, out }) => {
      const result = await channel.request(method, params);
      out.data(result, () => describe(result));
      return DONE;
    },
  });
  if (verb === "list" && name === undefined) {
    return request("settings.list", where, (result) => table((result as { settings: Setting[] }).settings));
  }
  if (verb === "get" && name !== undefined && value === undefined) {
    return request("settings.get", { ...where, name }, (result) => `${(result as Setting).value}\n`);
  }
  if (verb === "set" && name !== undefined && value !== undefined && extra.length === 0) {
    return request("settings.set", { ...where, name, value }, (result) => changed(result as Setting));
  }
  throw new CommandError(misuse(verb), USAGE);
}

function misuse(verb: string | undefined): string {
  if (verb === "get") return "uw settings get takes one setting.\n  uw settings get agent.approval-mode";
  if (verb === "set") return "uw settings set takes a setting and a value.\n  uw settings set theme.grain 20";
  if (verb === "list") return "uw settings list takes no setting.\n  uw settings list --workspace Work";
  return "uw settings needs list, get, or set.\n  uw settings list\nRun uw settings --help for more.";
}

function changed(setting: Setting): string {
  const workspaces = setting.appliesTo;
  if (workspaces.length === 1) return `${setting.name} is now ${setting.value} for ${workspaces[0]}.\n`;
  const names = `${workspaces.slice(0, -1).join(", ")} and ${workspaces.at(-1)}`;
  return `${setting.name} is now ${setting.value} for ${names} (same profile).\n`;
}

function table(rows: Setting[]): string {
  const lines = [["Setting", "Value", "Takes", "Applies to"], ...rows.map((row) => [
    row.name, String(row.value), row.takes, row.appliesTo.join(", "),
  ])];
  const widths = [0, 1, 2].map((column) => Math.max(...lines.map((line) => line[column]!.length)) + 2);
  return lines.map((line) => line.map((cell, column) => cell.padEnd(widths[column] ?? 0)).join("").trimEnd()).join("\n") + "\n";
}
