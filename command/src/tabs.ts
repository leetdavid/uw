/** uw tabs: list, show, move, and close your tabs, and rename tasks. See docs/plans/tab-tools.md. */

import type { Channel } from "./channel.ts";
import { options, type Command, type Plan } from "./cli.ts";
import { CommandError, DONE, USAGE } from "./errors.ts";

export const TABS_HELP = `uw tabs: list, show, move, and close your tabs, and rename tasks.

Usage:
  uw tabs list [--query <words>] [--workspace <name>] [--json]
  uw tabs select <tab>
  uw tabs move <tab>... (--task <task> | --new-task <name> | --scratchpad)
  uw tabs rename-task <task> <name>
  uw tabs close <tab>...
  uw tabs close-task <task>

A tab or a task is named by the id uw tabs list shows, and any unique start of
an id works, also after uw restarts. list shows the workspace used last, or the
one --workspace names; ids work in every workspace of its profile. Moving or
closing a tab takes the tabs under it along. Tabs move only within their
workspace, and a pinned tab or a terminal running a command is never closed.
Nothing asks first: these are your own commands.

Examples:
  uw tabs list --query flights
  uw tabs move 7d1f2a90 c33e8b15 --new-task "Hotels in Lisbon"
  uw tabs close 5b02e7c4 d94c1a38
  uw tabs select a81d
  uw tabs list --workspace Work --json
`;

interface Task {
  id: string;
  name: string;
}

interface Listed {
  id: string;
  title: string;
  url: string;
  place: string;
  workspace: string;
  task: Task | null;
  parent: string | null;
  depth: number;
  selected: boolean;
  kept: boolean;
  pinned: boolean;
  busy: boolean;
  seen: string | null;
}

interface List {
  workspace: string;
  tabs: Listed[];
  others: { workspace: string; tabs: number }[];
}

interface TabValue {
  id: string;
  title: string;
  url: string;
}

interface Done {
  tab?: TabValue;
  moved?: TabValue[];
  closed?: TabValue[];
  task?: Task;
  new?: boolean;
  scratchpad?: boolean;
  was?: string;
}

export const TABS: Command = { help: TABS_HELP, plan: async (args) => tabsPlan(args) };

const count = (n: number) => `${n} ${n === 1 ? "tab" : "tabs"}`;

function tabsPlan(args: string[]): Plan | null {
  const { values, positionals } = options(args, {
    workspace: { type: "string" },
    query: { type: "string" },
    task: { type: "string" },
    "new-task": { type: "string" },
    scratchpad: { type: "boolean", default: false },
  });
  if (values.help) return null;
  const [verb, ...rest] = positionals;
  const where = values.workspace === undefined ? {} : { workspace: values.workspace };
  const act = (method: string, params: object, said: (done: Done) => string): Plan => ({
    json: values.json,
    run: async ({ channel, out }) => {
      const done = await request(channel, method, { ...where, ...params }) as Done;
      out.data(done, () => `${said(done)}\n`);
      return DONE;
    },
  });
  const targets = [values.task !== undefined, values["new-task"] !== undefined, values.scratchpad].filter(Boolean).length;
  if (verb === "list" && rest.length === 0 && targets === 0) {
    const query = values.query?.trim() ?? "";
    return {
      json: values.json,
      run: async ({ channel, out }) => {
        const list = await request(channel, "tabs.list", { ...where, ...(query ? { query } : {}) }) as List;
        out.data(list, () => listText(list, query));
        return DONE;
      },
    };
  }
  if (values.query !== undefined) throw new CommandError(misuse(verb), USAGE);
  if (verb === "select" && rest.length === 1 && targets === 0) {
    return act("tabs.select", { tab: rest[0] }, (done) => `Switched to ${done.tab!.title || done.tab!.url}.`);
  }
  if (verb === "move" && rest.length > 0 && targets === 1) {
    const target = values.scratchpad ? { scratchpad: true }
      : values.task !== undefined ? { task: values.task } : { new_task: values["new-task"] };
    return act("tabs.move", { tabs: rest, ...target }, (done) => {
      const moved = count(done.moved!.length);
      if (done.scratchpad) return `Moved ${moved} to the Scratchpad.`;
      return done.new
        ? `Moved ${moved} to a new task, ${done.task!.name} (task ${done.task!.id}).`
        : `Moved ${moved} to ${done.task!.name}.`;
    });
  }
  if (verb === "rename-task" && rest.length === 2 && targets === 0) {
    return act("tabs.rename-task", { task: rest[0], name: rest[1] }, (done) => `Renamed ${done.was} to ${done.task!.name}.`);
  }
  if (verb === "close" && rest.length > 0 && targets === 0) {
    return act("tabs.close", { tabs: rest }, (done) =>
      done.task ? `Closed ${count(done.closed!.length)} in ${done.task.name}.` : `Closed ${count(done.closed!.length)}.`);
  }
  if (verb === "close-task" && rest.length === 1 && targets === 0) {
    return act("tabs.close-task", { task: rest[0] }, (done) =>
      `Closed the task ${done.task!.name} and its ${count(done.closed!.length)}.`);
  }
  throw new CommandError(misuse(verb), USAGE);
}

/** Asks uw, and words what it refused as the command's other errors are. */
async function request(channel: Channel, method: string, params: object): Promise<unknown> {
  try {
    return await channel.request(method, params);
  } catch (error) {
    if (error instanceof CommandError) throw new CommandError(`uw: ${error.message}`, error.exit);
    throw error;
  }
}

function misuse(verb: string | undefined): string {
  switch (verb) {
    case "list": return 'uw tabs list takes no tab; narrow it with --query.\n  uw tabs list --query "flights"';
    case "select": return "uw tabs select takes one tab's id.\n  uw tabs select a81d";
    case "move": return 'uw tabs move takes tabs and one of --task, --new-task, or --scratchpad.\n  uw tabs move 7d1f c33e --new-task "Hotels"';
    case "rename-task": return 'uw tabs rename-task takes a task\'s id and its new name.\n  uw tabs rename-task 51be "Lisbon stays"';
    case "close": return "uw tabs close takes the tabs' ids.\n  uw tabs close 5b02 d94c";
    case "close-task": return "uw tabs close-task takes one task's id.\n  uw tabs close-task 9c2e";
    default: return "uw tabs needs list, select, move, rename-task, close, or close-task.\n  uw tabs list\nRun uw tabs --help for more.";
  }
}

/** How long ago `seen` was, as the chat's agent reads it. */
function seenAgo(seen: string, now = Date.now()): string {
  const minutes = Math.floor((now - Date.parse(seen)) / 60000);
  const ago = (n: number, unit: string) => `${n} ${unit}${n === 1 ? "" : "s"} ago`;
  if (minutes < 1) return "now";
  if (minutes < 60) return ago(minutes, "minute");
  if (minutes < 60 * 24) return ago(Math.floor(minutes / 60), "hour");
  return ago(Math.floor(minutes / (60 * 24)), "day");
}

/** What a tab's address says of it: its site, "terminal", or a uw page. */
function site(url: string): string {
  try {
    const { protocol, hostname } = new URL(url);
    if (protocol === "chrome:") return hostname === "terminal" ? "terminal" : `uw://${hostname}`;
    return hostname.replace(/^www\./, "");
  } catch {
    return url;
  }
}

const MAX_TITLE = 40;

/** The list as the canvas frame "Tab tools: uw tabs in the terminal" draws it. */
function listText(list: List, query: string): string {
  if (!list.tabs.length) {
    return query ? `No tab in ${list.workspace} matches "${query}".\n` : `${list.workspace} has no tabs.\n`;
  }
  const rows = list.tabs.map((tab) => {
    const title = tab.title || tab.url;
    const cut = title.length > MAX_TITLE ? `${title.slice(0, MAX_TITLE - 1)}…` : title;
    const status = [tab.busy ? "busy" : "", tab.kept ? "kept" : "",
      tab.selected ? "selected" : tab.seen ? seenAgo(tab.seen) : ""].filter(Boolean).join(" · ");
    return { tab, start: `${" ".repeat(2 + 2 * tab.depth)}${tab.id}  ${cut}`, site: site(tab.url), status };
  });
  const startWidth = Math.max(...rows.map((row) => row.start.length)) + 3;
  const siteWidth = Math.max(...rows.map((row) => row.site.length)) + 3;
  const width = startWidth + siteWidth + Math.max(...rows.map((row) => row.status.length));
  const lines: string[] = [];
  let place = "";
  for (const row of rows) {
    if (row.tab.place !== place) {
      place = row.tab.place;
      const label = row.tab.task ? `task ${row.tab.task.id}` : "";
      lines.push(label ? `${place.padEnd(Math.max(width - label.length, place.length + 3))}${label}` : place);
    }
    lines.push(`${row.start.padEnd(startWidth)}${row.site.padEnd(siteWidth)}${row.status}`.trimEnd());
  }
  const own = list.tabs.filter((tab) => !tab.pinned).length;
  let footer = `${count(own)} in ${list.workspace}.`;
  if (list.others.length) {
    const others = list.others.map((other) => `${other.workspace} has ${other.tabs}`).join(", ");
    const name = list.others.length === 1 ? list.others[0]!.workspace : "<name>";
    footer += ` ${others}: uw tabs list --workspace ${/\s/.test(name) ? `"${name}"` : name}`;
  }
  lines.push(footer);
  return `${lines.join("\n")}\n`;
}
