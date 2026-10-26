/** A chat's steps and approvals in plain words, as the chat page words them. */

export interface Step {
  kind: string;
  tool: string;
  args: Record<string, unknown>;
  result: Record<string, unknown>;
  text: string;
}

export interface Approval {
  tab: number;
  site: string;
  action: string;
  reason: string;
  folder: string;
  command: string;
  text: string;
  keys: string[];
  connector: string;
  tool: string;
  arguments: Record<string, unknown>;
  canAlwaysAllow: boolean;
  sensitive: boolean;
  file: string;
}

/** A choice of an approval card: what the command sends, what it says, and how long it lasts. */
export interface Choice {
  choice: "allow" | "allow-for-chat" | "always-allow" | "decline" | "stop";
  title: string;
  lasts?: string;
}

/** An approval card in parts, as the chat page draws it, and its choices, numbered from 1. */
export interface Card {
  label: string;
  question: string;
  /** The command, keys, or a tool's arguments. */
  detail: string[];
  reason: string;
  choices: Choice[];
  /** The parts as printed lines. */
  lines: string[];
}

/** A page's address as a step names it: its site and path, without the scheme or a closing slash. */
function shortAddress(url: unknown): string {
  try {
    const { hostname, pathname } = new URL(String(url));
    return (hostname.replace(/^www\./, "") + pathname).replace(/\/$/, "");
  } catch {
    return String(url);
  }
}

function keysSent(args: Record<string, unknown>): string {
  const keys = Array.isArray(args.keys) ? args.keys.map(String) : [];
  return [...(args.text ? [`"${String(args.text)}"`] : []), ...keys].join(" ");
}

/** A connector's tool as the model calls it: "linear__save_issue". */
function connectorTool(tool: string): { connector: string; tool: string } | null {
  const at = tool.indexOf("__");
  return at > 0 ? { connector: tool.slice(0, at), tool: tool.slice(at + 2) } : null;
}

function firstLine(text: string): string {
  const line = text.trim().split("\n")[0] ?? "";
  return line.length > 80 ? `${line.slice(0, 79)}…` : line;
}

/** One tool step in plain words, and where it happened, such as "tab 1". */
export interface Described {
  what: string;
  where: string;
  /** False when the step failed or the user declined it. */
  done: boolean;
}

export function describeStep(step: Step): Described {
  const { args, result } = step;
  const did = (what: string, where = ""): Described => ({ what, where, done: true });
  const tab = (value: unknown) => `tab ${String(value)}`;
  const error = typeof result.error === "string" ? result.error : "";
  const called = connectorTool(step.tool);
  if (error.startsWith("The user did not approve")) {
    const what = called
      ? `Didn't run ${called.tool} in ${called.connector}`
      : step.tool === "run" ? `Didn't run ${String(args.command)}` : `Didn't send ${keysSent(args)}`;
    return { what, where: "", done: false };
  }
  if (error) return { what: `${step.tool}: ${error}`, where: "", done: false };
  const element = String(result.element ?? `element ${String(args.element)}`);
  switch (step.tool) {
    case "open": return did(`Opened ${shortAddress(args.url)}`, tab(result.tab));
    case "navigate": return did(`Went to ${shortAddress(args.url)}`, tab(args.tab));
    case "read": return did(`Read ${String(result.title || shortAddress(result.url))}`, tab(args.tab));
    case "click": return did(`Clicked ${element}`, tab(args.tab));
    case "type": return did(`Typed into ${element}`, tab(args.tab));
    case "run":
      return result.running
        ? did(`Running ${String(args.command)}`, `${tab(result.tab)} · ${String(result.waited)}`)
        : did(`Ran ${String(args.command)}`, tab(result.tab));
    case "screen": return did(`Read ${String(result.title || result.folder)}`, tab(args.tab));
    case "keys": return did(`Sent ${keysSent(args)}`, tab(args.tab));
    case "recall": return did(`Looked again at ${String((result.result as { title?: string } | undefined)?.title || `step ${String(args.step)}`)}`);
    case "skill": return did(args.file ? `Read ${String(args.file)} of the ${String(args.name)} skill` : `Used the ${String(args.name)} skill`);
    case "schedule": return did("Filled in the schedule card");
    case "trigger": return did(`Left ${String(args.command)} running as a trigger`);
    case "stop_trigger": return did("Stopped the trigger");
    case "save_skill": return did(`${result.saved ? "Saved" : "Did not save"} the ${String(args.name)} skill`);
    case "connector": {
      const tools = Array.isArray(result.tools) ? result.tools.length : 0;
      return did(`Loaded ${String(result.connector)} · ${tools} tool${tools === 1 ? "" : "s"}`, "connector");
    }
    case "prompt": return did(`Used the ${String(args.name)} prompt`, "connector");
    default: {
      if (!called) return did(step.tool);
      const images = typeof result.images === "number" ? `${result.images} image${result.images === 1 ? "" : "s"}` : "";
      const said = [firstLine(String(result.text ?? "")), images].filter(Boolean);
      return did([`Used ${called.connector} · ${called.tool}`, ...said].join(" · "), "connector");
    }
  }
}

const MEMORY_TOOLS = new Set(["remember", "forget", "search_memory"]);

/** A step of the memory tools, which shows only as counts once the agent answers. */
export function isMemoryStep(step: Step): boolean {
  return step.kind === "tool" && MEMORY_TOOLS.has(step.tool);
}

/** The memories the agent saved, forgot, and found before its answer, by id, as the chat page counts them. */
export interface MemoryTally {
  saved: number[];
  forgot: number[];
  recalled: number[];
}

export const NO_MEMORY: MemoryTally = { saved: [], forgot: [], recalled: [] };

type Remembered = { id: number };

export function countMemory(tally: MemoryTally, step: Step): MemoryTally {
  const { result } = step;
  const kept = result.kept as Remembered | undefined;
  const replaced = result.replaced as Remembered | undefined;
  const forgot = result.forgot as Remembered | undefined;
  const found = Array.isArray(result.memories) ? (result.memories as Remembered[]) : [];
  const saved = kept ? [...tally.saved, kept.id] : tally.saved;
  return {
    saved,
    forgot: [...tally.forgot, ...[replaced, forgot].filter((memory) => memory).map((memory) => memory!.id)],
    recalled: [...tally.recalled, ...found.map((memory) => memory.id)
      .filter((id, at, ids) => ids.indexOf(id) === at && !saved.includes(id) && !tally.recalled.includes(id))],
  };
}

/** "Saved 2 memories", "Forgot 1 memory", and "Recalled 3 memories", for the counts that are not 0. */
export function memoryLines(tally: MemoryTally): string[] {
  const line = (verb: string, ids: number[]) => `${verb} ${ids.length} ${ids.length === 1 ? "memory" : "memories"}`;
  return [["Saved", tally.saved], ["Forgot", tally.forgot], ["Recalled", tally.recalled]]
    .filter(([, ids]) => (ids as number[]).length > 0)
    .map(([verb, ids]) => line(verb as string, ids as number[]));
}

/** One tool step in plain words, with the tab it happened in. */
export function stepLine(step: Step): string {
  const { what, where } = describeStep(step);
  return where.startsWith("tab ") ? `${what} in ${where}` : what;
}

export function approvalCard(approval: Approval): Card {
  const connector = Boolean(approval.connector);
  const terminal = Boolean(approval.folder);
  const label = connector
    ? `${approval.connector} · ${approval.tool}`
    : terminal
      ? `Terminal · ${approval.tab > 0 ? `tab ${approval.tab} · ` : ""}${approval.folder}`
      : `${approval.site} · tab ${approval.tab}`;
  const question = connector
    ? `Run ${approval.tool} in ${approval.connector}?`
    : terminal
      ? approval.sensitive
        ? `Let the agent read ${approval.file || "a sensitive file"}?`
        : approval.command ? "Run this command?" : "Send keys to the running program?"
      : `${approval.action}?`;
  const detail = connector
    ? Object.entries(approval.arguments).map(([key, value]) => `  ${key}: ${typeof value === "string" ? value : JSON.stringify(value)}`)
    : terminal
      ? [approval.command ? `  % ${approval.command}` : `  ${keysSent({ text: approval.text, keys: approval.keys })}`]
      : [];
  const choices: Choice[] = connector
    ? [
      { choice: "allow", title: "Allow once" },
      { choice: "allow-for-chat", title: "Allow for this chat", lasts: "until this chat ends" },
      ...(approval.canAlwaysAllow
        ? [{ choice: "always-allow" as const, title: "Always allow", lasts: "in every chat, until you take it back" }]
        : []),
      { choice: "decline", title: "Not this one" },
      { choice: "stop", title: "Stop the chat" },
    ]
    : [
      { choice: "allow", title: "Approve" },
      { choice: "decline", title: "Not this one" },
      { choice: "stop", title: "Stop the chat" },
    ];
  const reason = approval.reason;
  return { label, question, detail, reason, choices, lines: [label, question, ...detail, ...(reason ? [reason] : [])] };
}
