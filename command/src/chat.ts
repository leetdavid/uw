/** uw chat, uw chats, and uw approve: run uw's agent from a terminal. */

import { options, type Command, type Context, type Input, type Plan } from "./cli.ts";
import { CommandError, DONE, FAILED, NEEDS_YOU, USAGE } from "./errors.ts";
import { approvalCard, stepLine, type Approval, type Choice, type Step } from "./steps.ts";

export const CHAT_HELP = `uw chat: ask uw's agent to do something, as in uw://chat.

Usage:
  uw chat ["<request>"]                in a terminal, opens the terminal interface
  uw chat -p "<request>" [options]     prints the answer instead
  uw chat -c -p "<request>"            continue the latest chat in the workspace
  uw chat -r <chat> -p ["<request>"]   pick up a chat, such as after uw approve

The chat is an ordinary saved chat in uw, marked "From terminal". Its tabs open
out of sight. In the terminal interface, Enter sends or steers, / lists skills
and connector prompts, Esc stops the agent, Ctrl-O opens the chat in uw, and
Ctrl-C leaves. With -p, or with no terminal, steps go to stderr and the answer
goes to stdout as Markdown. With a terminal, a step that needs approval asks
here; with none, the command prints the step and exits with 3, and uw approve
answers it. Ctrl-C stops the chat. Text piped in with a request is attached
material, never your instruction.

Options:
  -p, --print            Print the answer and exit.
  --mode <mode>          ask-first, auto, or full-auto, for this chat only.
  --model <model>        A model of the chat's provider.
  --effort <level>       One of the model's effort levels.
  --timeout <duration>   Stop the chat after this long, such as 90s or 10m.
  --workspace <name>     Where the chat runs. Default: the one used last.
  --json                 Print one result object at the end.

Examples:
  uw chat -p "Did my latest Vercel deploy succeed?"
  uw chat -p --mode full-auto "Close Linear issues in Web older than 90 days"
  cat notes.md | uw chat -p "File these as Linear issues"
  uw chat -r 3f9c2e18 -p
`;

export const CHATS_HELP = `uw chats: list the chats of a workspace, newest first.

Usage:
  uw chats [--workspace <name>] [--json]

Examples:
  uw chats
  uw chats --workspace Personal --json
`;

export const APPROVE_HELP = `uw approve: answer the step a chat waits on.

Usage:
  uw approve <chat>                                 show what waits
  uw approve <chat> allow
  uw approve <chat> allow-for-chat | always-allow   a connector's tool
  uw approve <chat> decline [--note "<what to do instead>"]
  uw approve <chat> stop

Then carry on with: uw chat -r <chat> -p

Examples:
  uw approve 3f9c2e18
  uw approve 3f9c2e18 allow
  uw approve 3f9c2e18 decline --note "Cancel the yearly plan instead"
`;

const MODES = ["ask-first", "auto", "full-auto"];
const FULL_AUTO = "▲ Full auto: the agent won't ask before changes on your sites, commands, or connector tools. "
  + "It still asks before reading a sensitive file.";
const CHOICES = ["allow", "allow-for-chat", "always-allow", "decline", "stop"];

/** A duration such as 90s, 10m, or 1h, in milliseconds. */
export function duration(text: string): number {
  const match = /^(\d+(?:\.\d+)?)(s|m|h)?$/.exec(text);
  if (!match) throw new CommandError(`--timeout takes a duration such as 90s, 10m, or 1h, not ${text}.`, USAGE);
  return Number(match[1]) * { s: 1000, m: 60_000, h: 3_600_000 }[match[2] as "s" | "m" | "h" ?? "s"];
}

interface Ended {
  status: "answered" | "failed" | "stopped";
  text: string;
  cost: number | null;
}

export const CHAT: Command = { help: CHAT_HELP, plan: chatPlan };

async function chatPlan(args: string[], input: Input): Promise<Plan | null> {
  const { values, positionals } = options(args, {
    print: { type: "boolean", short: "p", default: false },
    mode: { type: "string" },
    model: { type: "string" },
    effort: { type: "string" },
    continue: { type: "boolean", short: "c", default: false },
    resume: { type: "string", short: "r" },
    timeout: { type: "string" },
    workspace: { type: "string" },
  });
  if (values.help) return null;
  if (values.mode !== undefined && !MODES.includes(values.mode)) {
    throw new CommandError(`--mode takes ask-first, auto, or full-auto, not ${values.mode}.`, USAGE);
  }
  if (values.continue && values.resume !== undefined) {
    throw new CommandError("uw chat takes -c or -r, not both.", USAGE);
  }
  const limit = values.timeout === undefined ? undefined : duration(values.timeout);
  const piped = input.terminal ? "" : await input.stdin();
  let text = positionals.join(" ").trim();
  let material = "";
  if (text && piped.trim()) material = piped;
  else if (!text) text = piped.trim();
  const picking = values.continue || values.resume !== undefined;
  const screen = values.print || values.json ? undefined : input.screen;
  if (!text && !picking && !screen) {
    throw new CommandError('uw chat needs a request.\n  uw chat -p "Did my latest Vercel deploy succeed?"', USAGE);
  }
  const params = {
    ...(values.workspace === undefined ? {} : { workspace: values.workspace }),
    ...(text ? { text } : {}),
    ...(material ? { material } : {}),
    ...(values.mode === undefined ? {} : { mode: values.mode }),
    ...(values.model === undefined ? {} : { model: values.model }),
    ...(values.effort === undefined ? {} : { effort: values.effort }),
    ...(values.resume === undefined ? {} : { chat: values.resume }),
  };
  const method = picking ? "chats.resume" : "chats.start";
  if (screen) {
    return {
      json: false,
      run: async (context) => {
        if (values.mode === "full-auto") context.out.say(FULL_AUTO);
        const timeout = limit === undefined ? {} : { limit: { after: limit, said: describe(limit) } };
        // Ink and its layout engine load only for the terminal interface.
        const { runInterface } = await import("./tui.ts");
        return runInterface(screen, { channel: context.channel, method, params, ...timeout });
      },
    };
  }
  return { json: values.json, run: (context) => runChat(context, method, params, values.mode, limit, input) };
}

async function runChat(context: Context, method: string, params: object, mode: string | undefined,
  limit: number | undefined, input: Input): Promise<number> {
  const { channel, out, terminal } = context;
  if (mode === "full-auto") out.say(FULL_AUTO);
  let chat = "";
  let settle: (exit: number) => void = () => {};
  const done = new Promise<number>((resolve) => { settle = resolve; });
  const finish = (exit: number, result: object) => {
    out.data({ chat, ...result }, () => "");
    settle(exit);
  };
  let answering = false;
  // uw's reply and its first notifications can come together, so the chat's
  // id comes from whichever arrives first.
  const learn = (id: unknown) => {
    if (chat || typeof id !== "string") return;
    chat = id;
    out.say(chat.slice(0, 8));
  };
  channel.onNotification((name, raw) => {
    learn(raw.chat);
    if (name === "chat.step") {
      const step = raw as unknown as Step;
      if (step.kind === "tool") out.say(stepLine(step));
    } else if (name === "chat.approval") {
      const approval = (raw as { approval: Approval | null }).approval;
      if (!approval || answering) return;
      if (!terminal) {
        void channel.request("chats.detach", { chat }).finally(() => {
          const { lines } = approvalCard(approval);
          out.say(`Waits for your approval: ${lines.join(" · ")}`);
          out.say(`Answer with: uw approve ${chat.slice(0, 8)} allow, or decline --note "…"`);
          finish(NEEDS_YOU, { status: "needs-you", answer: null, waiting: approval, cost: null });
        });
        return;
      }
      answering = true;
      void ask(context, input, chat, approval).finally(() => { answering = false; });
    } else if (name === "chat.end") {
      const ended = raw as unknown as Ended;
      if (ended.status === "answered") {
        out.line(ended.text);
        finish(DONE, { status: "answered", answer: ended.text, waiting: null, cost: ended.cost });
      } else {
        out.say(ended.status === "failed" ? `Didn't finish: ${ended.text}` : "The chat stopped.");
        finish(FAILED, { status: ended.status, answer: null, waiting: null, cost: ended.cost });
      }
    }
  });
  channel.onClose(() => {
    out.say("uw closed the connection, so the chat stopped before it finished.");
    settle(FAILED);
  });
  learn((await channel.request(method, params) as { chat: string }).chat);
  const timer = limit === undefined ? undefined : setTimeout(() => {
    void channel.request("chats.stop", { chat });
    out.say(`Stopped after ${describe(limit)} (--timeout).`);
    settle(FAILED);
  }, limit);
  input.onInterrupt?.(() => {
    void channel.request("chats.stop", { chat });
    out.say(`You stopped the chat. Carry on with: uw chat -r ${chat.slice(0, 8)}`);
    settle(FAILED);
  });
  const exit = await done;
  if (timer) clearTimeout(timer);
  return exit;
}

function describe(milliseconds: number): string {
  if (milliseconds % 3_600_000 === 0) return `${milliseconds / 3_600_000} hour${milliseconds === 3_600_000 ? "" : "s"}`;
  if (milliseconds % 60_000 === 0) return `${milliseconds / 60_000} minute${milliseconds === 60_000 ? "" : "s"}`;
  return `${Math.round(milliseconds / 1000)} seconds`;
}

/** Shows the approval card on stderr and answers with what the person picks or writes. */
async function ask(context: Context, input: Input, chat: string, approval: Approval): Promise<void> {
  const { lines, choices } = approvalCard(approval);
  context.out.say(["", ...lines, ...choices.map((choice, at) => `  ${at + 1}  ${choice.title}`),
    "  Or type what to do instead, and the agent won't do this."].join("\n"));
  const typed = (await (input.ask?.("> ") ?? Promise.resolve(""))).trim();
  const picked: Choice | undefined = /^\d+$/.test(typed) ? choices[Number(typed) - 1] : undefined;
  const answer = picked ? { choice: picked.choice } : { choice: "decline", note: typed };
  context.out.say(picked ? picked.title : typed ? "Not this one, with your note" : "Not this one");
  await context.channel.request("chats.approve", { chat, ...answer });
}

export const CHATS: Command = {
  help: CHATS_HELP,
  plan: async (args) => {
    const { values, positionals } = options(args, { workspace: { type: "string" } });
    if (values.help) return null;
    if (positionals.length) throw new CommandError("uw chats takes no arguments.\n  uw chats", USAGE);
    const where = values.workspace === undefined ? {} : { workspace: values.workspace };
    return { json: values.json, run: async ({ channel, out }) => {
      const listed = await channel.request("chats.list", where) as { chats: Listed[] };
      out.data(listed, () => chatTable(listed.chats));
      return DONE;
    } };
  },
};

interface Listed {
  id: string;
  title: string;
  state: string;
  fromTerminal: boolean;
  updated: number;
}

function chatTable(chats: Listed[]): string {
  if (!chats.length) return 'No chats yet.\n  uw chat -p "Did my latest Vercel deploy succeed?"\n';
  const rows = [["ID", "Title", "State", "Started", "Last"], ...chats.map((chat) => [
    chat.id.slice(0, 8),
    chat.title.length > 40 ? `${chat.title.slice(0, 39)}…` : chat.title,
    chat.state[0]!.toUpperCase() + chat.state.slice(1),
    chat.fromTerminal ? "terminal" : "in uw",
    new Date(chat.updated).toLocaleDateString("en", { month: "short", day: "numeric" }),
  ])];
  const widths = [0, 1, 2, 3].map((column) => Math.max(...rows.map((row) => row[column]!.length)) + 2);
  return rows.map((row) => row.map((cell, column) => cell.padEnd(widths[column] ?? 0)).join("").trimEnd()).join("\n") + "\n";
}

export const APPROVE: Command = {
  help: APPROVE_HELP,
  plan: async (args) => {
    const { values, positionals } = options(args, { note: { type: "string" } });
    if (values.help) return null;
    const [chat, choice, ...extra] = positionals;
    if (chat === undefined || extra.length || (choice !== undefined && !CHOICES.includes(choice))) {
      throw new CommandError("uw approve takes a chat and, to answer, allow, allow-for-chat, always-allow, decline, or stop.\n  uw approve 3f9c2e18 allow", USAGE);
    }
    if (choice === undefined) {
      return { json: values.json, run: async ({ channel, out }) => {
        const { waiting } = await channel.request("chats.waiting", { chat }) as { waiting: Approval | null };
        out.data({ chat, waiting }, () => waiting
          ? `${approvalCard(waiting).lines.join("\n")}\nAnswer with: uw approve ${chat} allow, or decline --note "…"\n`
          : `Nothing in ${chat} waits for approval.\n`);
        return DONE;
      } };
    }
    return { json: values.json, run: async ({ channel, out }) => {
      const answered = await channel.request("chats.approve", { chat, choice, ...(values.note ? { note: values.note } : {}) });
      out.data(answered, () => `${choice === "decline" ? "Declined" : choice === "stop" ? "Stopped" : "Allowed"}. Carry on with: uw chat -r ${chat} -p\n`);
      return DONE;
    } };
  },
};
