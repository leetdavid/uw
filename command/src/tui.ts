/**
 * The terminal interface of uw chat, like Claude Code's: the chat's steps as they happen, the answer as Markdown,
 * approval cards, a composer that steers, the / menu, and a status line. See docs/plans/uw-command.md#the-terminal-interface.
 */

import { Box, Static, Text, render, useAnimation, useApp, useInput, useWindowSize, type Key } from "ink";
import { createElement as h, useEffect, useReducer, useRef, useState, type ReactNode } from "react";
import type { Channel } from "./channel.ts";
import { CommandError, DONE, FAILED } from "./errors.ts";
import { Markdown } from "./markdown.ts";
import { COLOR } from "./palette.ts";
import { approvalCard, describeStep, type Approval, type Choice, type Step } from "./steps.ts";

/** The terminal's screen, where the interface draws and reads keys. */
export interface Screen {
  stdin: NodeJS.ReadStream;
  stdout: NodeJS.WriteStream;
}

export type ChatChannel = Pick<Channel, "request" | "onNotification" | "onClose">;

export interface ChatProps {
  channel: ChatChannel;
  /** chats.start or chats.resume, and what it asks. */
  method: string;
  params: object;
  /** --timeout in milliseconds, and as the person wrote it. */
  limit?: { after: number; said: string };
}

export async function runInterface(screen: Screen, props: ChatProps): Promise<number> {
  const app = render(h(ChatApp, props), { stdin: screen.stdin, stdout: screen.stdout, exitOnCtrlC: false, patchConsole: false });
  return Number(await app.waitUntilExit());
}

export interface Status {
  model: string;
  effort: string;
  mode: string;
  cost: number | null;
}

type Line =
  | { kind: "header"; chat: string; workspace: string }
  | { kind: "user"; text: string }
  | { kind: "step"; what: string; where: string; done: boolean }
  | { kind: "answer"; text: string }
  | { kind: "said"; text: string; color: string };

export interface ChatState {
  lines: Line[];
  chat: string;
  workspace: string;
  working: boolean;
  waiting: Approval | null;
  status: Status | null;
}

export type ChatEvent =
  | { type: "started"; chat: string; workspace: string }
  | { type: "notified"; method: string; params: Record<string, unknown> }
  | { type: "said"; text: string; color: string };

export const START: ChatState = { lines: [], chat: "", workspace: "", working: false, waiting: null, status: null };

/** The chat as uw's notifications change it. */
export function reduce(state: ChatState, event: ChatEvent): ChatState {
  const add = (line: Line, change: Partial<ChatState> = {}): ChatState => ({ ...state, ...change, lines: [...state.lines, line] });
  if (event.type === "started") {
    return add({ kind: "header", chat: event.chat, workspace: event.workspace }, { chat: event.chat, workspace: event.workspace });
  }
  if (event.type === "said") return add({ kind: "said", text: event.text, color: event.color });
  const { method, params } = event;
  if (method === "chat.status") return { ...state, status: params as unknown as Status };
  if (method === "chat.approval") return { ...state, waiting: (params.approval as Approval | null) ?? null };
  if (method === "chat.step") {
    const step = params as unknown as Step;
    if (step.kind === "user") return add({ kind: "user", text: step.text }, { working: true });
    if (step.kind === "tool") return add({ kind: "step", ...describeStep(step) });
    return state;
  }
  if (method === "chat.end") {
    const ended = params as { status: string; text: string };
    const idle = { working: false, waiting: null };
    if (ended.status === "answered") return add({ kind: "answer", text: ended.text }, idle);
    return add(ended.status === "failed"
      ? { kind: "said", text: `Didn't finish: ${ended.text}`, color: COLOR.red }
      : { kind: "said", text: "The chat stopped.", color: COLOR.dim }, idle);
  }
  return state;
}

/** A row of the / menu: a skill the user may start, or a connector's prompt. */
export interface MenuItem {
  command: string;
  description: string;
  onlyYou: boolean;
  /** What to type after the command. */
  hint: string;
  prompt?: { connector: string; name: string; arguments: { name: string; required: boolean }[] };
}

interface Listed {
  skills: { name: string; description: string; onlyYou: boolean; hint: string }[];
  prompts: { connector: string; name: string; description: string; arguments: { name: string; required: boolean }[] }[];
}

export function menuItems(listed: Listed): MenuItem[] {
  return [
    ...listed.skills.map((skill) => ({ command: skill.name, description: skill.description, onlyYou: skill.onlyYou, hint: skill.hint })),
    ...listed.prompts.map((prompt) => ({
      command: `${prompt.connector}:${prompt.name}`,
      description: prompt.description,
      onlyYou: false,
      hint: prompt.arguments.map((argument) => `<${argument.name}>`).join(" "),
      prompt: { connector: prompt.connector, name: prompt.name, arguments: prompt.arguments },
    })),
  ];
}

/** The rows the menu shows while the composer holds "/" and the start of a command. */
export function shownItems(draft: string, items: MenuItem[]): MenuItem[] {
  if (!draft.startsWith("/") || draft.includes(" ")) return [];
  return items.filter((item) => item.command.startsWith(draft.slice(1)));
}

/** A prompt's arguments from the words after its command, in order, the last taking the rest, as Claude Code passes an MCP prompt's. */
export function promptValues(prompt: NonNullable<MenuItem["prompt"]>, rest: string): { values: Record<string, string>; missing: string[] } {
  const words = rest.trim() ? rest.trim().split(/\s+/) : [];
  const values: Record<string, string> = {};
  prompt.arguments.forEach((argument, at) => {
    const last = at === prompt.arguments.length - 1;
    const value = last ? words.slice(at).join(" ") : words[at];
    if (value) values[argument.name] = value;
  });
  const missing = prompt.arguments.filter((argument) => argument.required && !values[argument.name]).map((argument) => argument.name);
  return { values, missing };
}

export interface Edited {
  value: string;
  cursor: number;
}

const EMPTY: Edited = { value: "", cursor: 0 };

/** One line of text as a key changes it, or null when the key is not for editing. */
export function edit(line: Edited, input: string, key: Key): Edited | null {
  const { value, cursor } = line;
  if (key.leftArrow) return { value, cursor: Math.max(0, cursor - 1) };
  if (key.rightArrow) return { value, cursor: Math.min(value.length, cursor + 1) };
  if (key.home || (key.ctrl && input === "a")) return { value, cursor: 0 };
  if (key.end || (key.ctrl && input === "e")) return { value, cursor: value.length };
  if (key.ctrl && input === "u") return { value: value.slice(cursor), cursor: 0 };
  if (key.backspace || key.delete) {
    return cursor === 0 ? line : { value: value.slice(0, cursor - 1) + value.slice(cursor), cursor: cursor - 1 };
  }
  if (key.ctrl || key.meta || key.escape || key.return || key.tab || key.upArrow || key.downArrow || !input) return null;
  const typed = input.replace(/\r?\n/g, " ");
  return { value: value.slice(0, cursor) + typed + value.slice(cursor), cursor: cursor + typed.length };
}

const MODES: Record<string, string> = { "ask-first": "Ask first", auto: "Auto", "full-auto": "Full auto" };

export function ChatApp({ channel, method, params, limit }: ChatProps): ReactNode {
  const app = useApp();
  const { columns } = useWindowSize();
  const [state, dispatch] = useReducer(reduce, START);
  const [draft, setDraft] = useState(EMPTY);
  const [note, setNote] = useState(EMPTY);
  const [lit, setLit] = useState(0);
  const [items, setItems] = useState<MenuItem[]>([]);
  const [leaving, setLeaving] = useState<number | null>(null);
  const chat = useRef("");
  chat.current = state.chat;
  const short = state.chat.slice(0, 8);

  const said = (text: string, color: string = COLOR.dim) => dispatch({ type: "said", text, color });
  const leave = (exit: number, text?: string, color?: string) => {
    if (text) said(text, color);
    setLeaving(exit);
  };
  const ask = (asked: string, more: object = {}) =>
    channel.request(asked, { chat: chat.current, ...more }).catch((error: Error) => {
      said(error.message, COLOR.red);
      return null;
    });
  const loadMenu = (id: string) => {
    void channel.request("chats.menu", { chat: id }).then((listed) => setItems(menuItems(listed as Listed)), () => {});
  };

  useEffect(() => {
    if (leaving !== null) app.exit(leaving);
  }, [leaving]);

  useEffect(() => {
    // uw's first notifications can come before its reply is read; they wait for it.
    let started = false;
    const early: ChatEvent[] = [];
    channel.onNotification((notified, notice) => {
      const event: ChatEvent = { type: "notified", method: notified, params: notice };
      if (started) dispatch(event);
      else early.push(event);
    });
    channel.onClose(() => leave(FAILED, "uw closed the connection, so the chat stopped.", COLOR.red));
    channel.request(method, { ...params, stay: true }).then((reply) => {
      const { chat: id, workspace } = reply as { chat: string; workspace: string };
      dispatch({ type: "started", chat: id, workspace });
      started = true;
      for (const event of early) dispatch(event);
      loadMenu(id);
    }, (error: Error) => leave(error instanceof CommandError ? error.exit : FAILED, error.message, COLOR.red));
    if (!limit) return;
    const timer = setTimeout(() => {
      void channel.request("chats.stop", { chat: chat.current }).catch(() => {});
      leave(FAILED, `Stopped after ${limit.said} (--timeout).`);
    }, limit.after);
    return () => clearTimeout(timer);
  }, []);

  useEffect(() => setLit(0), [state.waiting]);
  useEffect(() => {
    if (draft.value === "/" && state.chat) loadMenu(state.chat);
  }, [draft.value === "/"]);

  const menu = state.waiting ? [] : shownItems(draft.value, items);

  const send = (text: string) => {
    const message = text.trim();
    if (!message || !state.chat) return;
    const [command = "", ...rest] = message.slice(1).split(" ");
    const prompt = message.startsWith("/") ? items.find((item) => item.prompt && item.command === command)?.prompt : undefined;
    if (!prompt) {
      void ask("chats.send", { text: message });
      return;
    }
    const { values, missing } = promptValues(prompt, rest.join(" "));
    if (missing.length) {
      said(`/${command} takes ${missing.join(", ")}: /${command} ${prompt.arguments.map((argument) => `<${argument.name}>`).join(" ")}`, COLOR.red);
      return;
    }
    void ask("chats.prompt", { connector: prompt.connector, prompt: prompt.name, values });
  };

  const pick = (item: MenuItem, enter: boolean) => {
    if (enter && !item.hint) {
      send(`/${item.command}`);
      setDraft(EMPTY);
      return;
    }
    const value = `/${item.command} `;
    setDraft({ value, cursor: value.length });
  };

  const answer = (choice: Choice["choice"], text = "") => {
    void ask("chats.approve", { choice, ...(text ? { note: text } : {}) });
    setNote(EMPTY);
  };

  useInput((input, key) => {
    if (leaving !== null) return;
    if (key.ctrl && input === "c") {
      if (state.working || state.waiting) {
        void ask("chats.stop");
        leave(FAILED, `You stopped the chat. Carry on with: uw chat -r ${short}`);
      } else {
        leave(DONE, state.chat ? `Carry on with: uw chat -r ${short}` : undefined);
      }
      return;
    }
    if (key.ctrl && input === "o") {
      if (state.chat) void ask("chats.open").then((shown) => shown && said("Opened the chat in uw."));
      return;
    }
    const rows = state.waiting ? approvalCard(state.waiting).choices : menu;
    if (rows.length && (key.upArrow || key.downArrow)) {
      setLit((at) => (at + (key.upArrow ? rows.length - 1 : 1)) % rows.length);
      return;
    }
    if (state.waiting) {
      const choices = rows as Choice[];
      if (!note.value && /^[1-9]$/.test(input) && Number(input) <= choices.length) {
        answer(choices[Number(input) - 1]!.choice);
      } else if (key.return) {
        if (note.value.trim()) answer("decline", note.value.trim());
        else answer(choices[lit]!.choice);
      } else {
        setNote(edit(note, input, key) ?? note);
      }
      return;
    }
    if (menu.length && (key.tab || key.return)) {
      pick(menu[Math.min(lit, menu.length - 1)]!, key.return);
      return;
    }
    if (key.escape) {
      if (menu.length) setDraft(EMPTY);
      else if (state.working) void ask("chats.stop");
      return;
    }
    if (key.return) {
      send(draft.value);
      setDraft(EMPTY);
      return;
    }
    const edited = edit(draft, input, key);
    if (edited) {
      setDraft(edited);
      setLit(0);
    }
  });

  return h(Box, { flexDirection: "column" },
    h(Static, { items: state.lines, children: (line: unknown, at: number) =>
      h(Box, { key: at, width: columns }, drawLine(line as Line, state.lines[at - 1])) }),
    state.working && !state.waiting ? h(Working) : null,
    state.waiting
      ? h(Card, { approval: state.waiting, lit, note })
      : h(Composer, { draft, menu, lit: Math.min(lit, Math.max(0, menu.length - 1)), working: state.working }),
    h(StatusLine, { state, columns }));
}

function drawLine(line: Line, before: Line | undefined): ReactNode {
  switch (line.kind) {
    case "header":
      return h(Box, { flexGrow: 1, borderStyle: "round", borderColor: COLOR.hairline, paddingX: 1, justifyContent: "space-between" },
        h(Text, null,
          h(Text, { bold: true, color: "white" }, "uw chat"),
          h(Text, { color: COLOR.faint }, " · "),
          h(Text, { color: COLOR.text }, line.workspace),
          h(Text, { color: COLOR.faint }, " · "),
          h(Text, { color: COLOR.dim }, "From terminal")),
        h(Text, { color: COLOR.faint }, line.chat.slice(0, 8)));
    case "user":
      return h(Box, { flexGrow: 1, marginTop: 1, backgroundColor: COLOR.fill, paddingX: 1 },
        h(Text, { color: COLOR.primary }, "› "),
        h(Text, { color: COLOR.text }, line.text));
    case "step":
      return h(Box, { flexGrow: 1, marginTop: before?.kind === "step" ? 0 : 1, justifyContent: "space-between", gap: 2 },
        h(Text, null,
          h(Text, { color: line.done ? COLOR.green : COLOR.red }, line.done ? "✓ " : "× "),
          h(Text, { color: COLOR.text }, line.what)),
        h(Box, { flexShrink: 0 }, h(Text, { color: COLOR.faint }, line.where)));
    case "answer":
      return h(Box, { marginTop: 1, paddingLeft: 2 }, h(Markdown, { text: line.text }));
    case "said":
      return h(Box, { marginTop: 1 }, h(Text, { color: line.color }, line.text));
  }
}

const SPINNER = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

function Working(): ReactNode {
  const { frame } = useAnimation({ interval: 80 });
  return h(Box, { marginTop: 1 },
    h(Text, { color: COLOR.primary }, `${SPINNER[frame % SPINNER.length]} `),
    h(Text, { color: COLOR.dim }, "Working…"));
}

/** Keys in the fill, as "[Enter] send", among dim words. */
function Hint({ text }: { text: string }): ReactNode {
  return h(Text, { color: COLOR.dim }, text.split(/\[([^\]]+)\]/).map((part, at) =>
    at % 2 ? h(Text, { key: at, backgroundColor: COLOR.fill, color: COLOR.text }, ` ${part} `) : part));
}

function Editor({ line }: { line: Edited }): ReactNode {
  const { value, cursor } = line;
  return h(Text, { color: COLOR.text }, value.slice(0, cursor), h(Text, { inverse: true }, value[cursor] ?? " "), value.slice(cursor + 1));
}

function Composer({ draft, menu, lit, working }: { draft: Edited; menu: MenuItem[]; lit: number; working: boolean }): ReactNode {
  return h(Box, { flexDirection: "column", marginTop: 1 },
    menu.length ? h(Menu, { menu, lit }) : null,
    h(Box, { borderStyle: "round", borderColor: COLOR.composer, paddingX: 1 },
      h(Text, { color: COLOR.primary }, "› "),
      h(Editor, { line: draft })),
    menu.length
      ? h(Hint, { text: "[Tab] or [Enter] picks · [Esc] closes the menu" })
      : working ? h(Hint, { text: "[Enter] steers: it goes in after this step · [Esc] stops the chat" }) : null);
}

function Menu({ menu, lit }: { menu: MenuItem[]; lit: number }): ReactNode {
  const named = (item: MenuItem) => `/${item.command}${item.hint ? ` ${item.hint}` : ""}`;
  const width = Math.max(...menu.map((item) => named(item).length)) + 3;
  const row = (item: MenuItem) => {
    const at = menu.indexOf(item);
    return h(Box, { key: item.command, paddingX: 1, ...(at === lit ? { backgroundColor: COLOR.fill } : {}) },
      h(Box, { width, flexShrink: 0 }, h(Text, { color: "white" }, named(item))),
      item.onlyYou ? h(Text, { color: COLOR.primary }, "Only you  ") : null,
      h(Text, { color: COLOR.dim, wrap: "truncate-end" }, item.description));
  };
  const skills = menu.filter((item) => !item.prompt);
  const prompts = menu.filter((item) => item.prompt);
  return h(Box, { flexDirection: "column", borderStyle: "round", borderColor: COLOR.hairline },
    skills.length ? h(Text, { color: COLOR.faint }, " Skills") : null,
    ...skills.map(row),
    prompts.length ? h(Text, { color: COLOR.faint }, " Prompts") : null,
    ...prompts.map(row));
}

function Card({ approval, lit, note }: { approval: Approval; lit: number; note: Edited }): ReactNode {
  const card = approvalCard(approval);
  const [first = "", ...rest] = card.reason.split(/(?<=\.)\s+/);
  return h(Box, { flexDirection: "column", marginTop: 1 },
    h(Box, { flexDirection: "column", borderStyle: "round", borderColor: COLOR.card, paddingX: 1 },
      h(Text, { color: COLOR.dim }, card.label),
      h(Text, { bold: true, color: "white" }, card.question),
      card.detail.length
        ? h(Box, { flexDirection: "column", backgroundColor: COLOR.fill, paddingX: 1 },
          card.detail.map((line, at) => h(Text, { key: at, color: COLOR.text }, line.trim())))
        : null,
      card.reason
        ? h(Text, null,
          h(Text, { color: COLOR.amber }, "▲ "),
          h(Text, { bold: true, color: "white" }, first),
          rest.length ? h(Text, { color: COLOR.dim }, ` ${rest.join(" ")}`) : null)
        : null,
      h(Box, { flexDirection: "column", marginTop: 1 }, card.choices.map((choice, at) =>
        h(Box, { key: choice.choice, ...(at === lit ? { backgroundColor: COLOR.lit } : {}) },
          h(Text, { color: COLOR.amber }, at === lit ? "› " : "  "),
          h(Text, { color: at === lit ? COLOR.amber : COLOR.faint }, `${at + 1}  `),
          h(Text, { color: "white" }, choice.title),
          choice.lasts ? h(Text, { color: COLOR.faint }, `  ${choice.lasts}`) : null))),
      h(Box, { paddingLeft: 5 }, note.value
        ? h(Editor, { line: note })
        : h(Text, { color: COLOR.faint }, "Tell the agent why, or what to do instead"))),
    h(Hint, { text: `[↑] [↓] or [1]–[${card.choices.length}] to pick · [Enter] to answer · type a note, and [Enter] declines with it · [Ctrl-O] opens the chat in uw` }));
}

const KEYS = "[Enter] send · [/] skills · [Esc] stop · [Ctrl-O] open in uw";

function StatusLine({ state, columns }: { state: ChatState; columns: number }): ReactNode {
  const { status, workspace, working, waiting } = state;
  const words = [
    { text: status?.model ? `${status.model}${status.effort ? ` · ${status.effort}` : ""}` : "", color: COLOR.dim },
    { text: status ? MODES[status.mode] ?? status.mode : "", color: status?.mode === "full-auto" ? COLOR.amber : COLOR.dim },
    { text: workspace, color: COLOR.dim },
    { text: status?.cost != null ? `$${status.cost.toFixed(2)}` : "", color: COLOR.dim },
  ].filter((word) => word.text);
  const said = words.map((word) => word.text).join("   ");
  // The keys show when the line has room for them, as a key takes a space on each side.
  const keys = columns >= said.length + KEYS.replace(/[[\]]/g, " ").length + 2;
  return h(Box, { marginTop: 1, justifyContent: "space-between", gap: 2 },
    h(Text, { wrap: "truncate-end" }, words.map((word, at) =>
      h(Text, { key: at, color: word.color }, at ? `   ${word.text}` : word.text))),
    h(Box, { flexShrink: 0 }, waiting
      ? h(Text, { color: COLOR.amber }, "● Waits for your approval")
      : working
        ? h(Text, { color: COLOR.primary }, "Working…")
        : keys ? h(Hint, { text: KEYS }) : null));
}
