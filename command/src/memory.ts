/** uw memory: list and forget what uw's agent remembers about you. See docs/plans/agent-memory.md. */

import { options, type Command, type Plan } from "./cli.ts";
import { CommandError, DONE, USAGE } from "./errors.ts";

export const MEMORY_HELP = `uw memory: list and forget what uw's agent remembers about you.

Usage:
  uw memory list [--search <words>] [--workspace <name>] [--json]
  uw memory forget <id> [--workspace <name>] [--json]

Each workspace's profile has its own memory, one plain sentence a memory. The
agent keeps and forgets them as it works; reword them on uw://ai/memory. With
--search, a memory matches when it holds any of the words. To keep one chat out
of memory, use uw chat --no-memory. To pause memory, uw settings set
agent.memory false.

Examples:
  uw memory list
  uw memory list --search "seat flight"
  uw memory forget 12
  uw memory list --workspace Work --json
`;

interface Listed {
  id: number;
  text: string;
  chat: string;
  chatTitle: string;
  changed: number;
}

interface List {
  memories: Listed[];
  total: number;
  on: boolean;
  workspace: string;
}

export const MEMORY: Command = { help: MEMORY_HELP, plan: async (args) => memoryPlan(args) };

function memoryPlan(args: string[]): Plan | null {
  const { values, positionals } = options(args, { search: { type: "string" }, workspace: { type: "string" } });
  if (values.help) return null;
  const [verb, id, ...extra] = positionals;
  const where = values.workspace === undefined ? {} : { workspace: values.workspace };
  if (verb === "list" && id === undefined) {
    const search = values.search?.trim() ?? "";
    return { json: values.json, run: async ({ channel, out }) => {
      const listed = await channel.request("memory.list", { ...where, ...(search ? { search } : {}) }) as List;
      if (!listed.on) {
        out.say(`Memory is paused for ${listed.workspace || "this workspace"}: chats there recall and keep nothing.`);
      }
      out.data(listed, () => memoryTable(listed.memories, search));
      return DONE;
    } };
  }
  if (verb === "forget" && id !== undefined && /^\d+$/.test(id) && extra.length === 0 && values.search === undefined) {
    return { json: values.json, run: async ({ channel, out }) => {
      const forgot = await channel.request("memory.forget", { ...where, id: Number(id) }) as { forgot: { text: string } };
      out.data(forgot, () => `Forgot: ${forgot.forgot.text}\n`);
      return DONE;
    } };
  }
  throw new CommandError(misuse(verb), USAGE);
}

function misuse(verb: string | undefined): string {
  if (verb === "forget") return "uw memory forget takes one memory's id, as uw memory list shows it.\n  uw memory forget 12";
  if (verb === "list") return 'uw memory list takes no memory; search with --search.\n  uw memory list --search "bank"';
  return "uw memory needs list or forget.\n  uw memory list\nRun uw memory --help for more.";
}

function memoryTable(memories: Listed[], search: string): string {
  if (!memories.length) return search ? `No memory mentions "${search}".\n` : "No memories yet.\n";
  const rows = [["ID", "Memory", "From", "Changed"], ...memories.map((memory) => [
    String(memory.id),
    memory.text,
    memory.chat ? memory.chatTitle || "a deleted chat" : "",
    new Date(memory.changed).toLocaleDateString("en", { month: "short", day: "numeric" }),
  ])];
  const widths = [0, 1, 2].map((column) => Math.max(...rows.map((row) => row[column]!.length)) + 2);
  return rows.map((row) => row.map((cell, column) => cell.padEnd(widths[column] ?? 0)).join("").trimEnd()).join("\n") + "\n";
}
