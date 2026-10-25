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

/** A choice of an approval card: what the command sends, and what it says. */
export interface Choice {
  choice: "allow" | "allow-for-chat" | "always-allow" | "decline" | "stop";
  title: string;
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

/** One tool step in plain words. */
export function stepLine(step: Step): string {
  const { args, result } = step;
  const tab = (value: unknown) => `tab ${String(value)}`;
  const error = typeof result.error === "string" ? result.error : "";
  const called = connectorTool(step.tool);
  if (error.startsWith("The user did not approve")) {
    if (called) return `Didn't run ${called.tool} in ${called.connector}`;
    return step.tool === "run" ? `Didn't run ${String(args.command)}` : `Didn't send ${keysSent(args)}`;
  }
  if (error) return `${step.tool}: ${error}`;
  const element = String(result.element ?? `element ${String(args.element)}`);
  switch (step.tool) {
    case "open": return `Opened ${shortAddress(args.url)} in ${tab(result.tab)}`;
    case "navigate": return `Went to ${shortAddress(args.url)} in ${tab(args.tab)}`;
    case "read": return `Read ${String(result.title || shortAddress(result.url))} in ${tab(args.tab)}`;
    case "click": return `Clicked ${element} in ${tab(args.tab)}`;
    case "type": return `Typed into ${element} in ${tab(args.tab)}`;
    case "run":
      return result.running
        ? `Running ${String(args.command)} in ${tab(result.tab)} · ${String(result.waited)}`
        : `Ran ${String(args.command)} in ${tab(result.tab)}`;
    case "screen": return `Read ${String(result.title || result.folder)} in ${tab(args.tab)}`;
    case "keys": return `Sent ${keysSent(args)} in ${tab(args.tab)}`;
    case "recall": return `Looked again at ${String((result.result as { title?: string } | undefined)?.title || `step ${String(args.step)}`)}`;
    case "skill": return args.file ? `Read ${String(args.file)} of the ${String(args.name)} skill` : `Used the ${String(args.name)} skill`;
    case "schedule": return "Filled in the schedule card";
    case "trigger": return `Left ${String(args.command)} running as a trigger`;
    case "stop_trigger": return "Stopped the trigger";
    case "save_skill": return `${result.saved ? "Saved" : "Did not save"} the ${String(args.name)} skill`;
    case "connector": {
      const tools = Array.isArray(result.tools) ? result.tools.length : 0;
      return `Loaded ${String(result.connector)} · ${tools} tool${tools === 1 ? "" : "s"}`;
    }
    case "prompt": return `Used the ${String(args.name)} prompt`;
    default: {
      if (!called) return step.tool;
      const images = typeof result.images === "number" ? `${result.images} image${result.images === 1 ? "" : "s"}` : "";
      const said = [firstLine(String(result.text ?? "")), images].filter(Boolean);
      return [`Used ${called.connector} · ${called.tool}`, ...said].join(" · ");
    }
  }
}

/** An approval card's lines, and the choices it offers, numbered from 1. */
export function approvalCard(approval: Approval): { lines: string[]; choices: Choice[] } {
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
  const reason = approval.reason ? [approval.reason] : [];
  const choices: Choice[] = connector
    ? [
      { choice: "allow", title: "Allow once" },
      { choice: "allow-for-chat", title: "Allow for this chat" },
      ...(approval.canAlwaysAllow ? [{ choice: "always-allow" as const, title: "Always allow" }] : []),
      { choice: "decline", title: "Not this one" },
      { choice: "stop", title: "Stop the chat" },
    ]
    : [
      { choice: "allow", title: "Approve" },
      { choice: "decline", title: "Not this one" },
      { choice: "stop", title: "Stop the chat" },
    ];
  return { lines: [label, question, ...detail, ...reason], choices };
}
