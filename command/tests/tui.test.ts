import assert from "node:assert/strict";
import { test } from "node:test";
import { render, type Key } from "ink";
import { createElement as h } from "react";
import { ChatApp, edit, promptValues, type ChatChannel } from "../src/tui.ts";
import { FakeScreen, KEY, settle } from "./screen.ts";

const CHAT = "3f9c2e18abcdef";
const MENU = {
  skills: [{ name: "handoff", description: "Write a handoff for the next session", onlyYou: true, hint: "" }],
  prompts: [{ connector: "notes", name: "greet", description: "Greets someone.", arguments: [{ name: "name", required: true }] }],
};
const LINEAR = {
  tab: 0, site: "", action: "", reason: "Auto asks: save_issue isn't marked as only reading.", folder: "", command: "", text: "",
  keys: [], connector: "linear", tool: "save_issue", arguments: { title: "Build fails" }, canAlwaysAllow: true, sensitive: false, file: "",
};
const PAGE = { ...LINEAR, tab: 1, site: "dropbox.com", action: 'Click "Yes, cancel Dropbox Plus"', reason: "Ends your plan on Nov 14.", connector: "", tool: "", arguments: {} };

/** The interface on a fake terminal, talking to a fake uw that answers `replies`. */
async function open(params: object = { text: "Did my deploy succeed?" }) {
  const sent: { method: string; params: Record<string, unknown> }[] = [];
  let notified: (method: string, params: Record<string, unknown>) => void = () => {};
  const replies: Record<string, unknown> = { "chats.start": { chat: CHAT, workspace: "Work" }, "chats.menu": MENU };
  const channel: ChatChannel = {
    request: async (method, given = {}) => {
      sent.push({ method, params: given as Record<string, unknown> });
      return replies[method] ?? {};
    },
    onNotification: (heard) => {
      notified = heard;
    },
    onClose: () => {},
  };
  const terminal = new FakeScreen();
  const app = render(h(ChatApp, { channel, method: "chats.start", params }), {
    ...terminal.screen, exitOnCtrlC: false, patchConsole: false,
  });
  await settle();
  const notify = async (method: string, notice: Record<string, unknown>) => {
    notified(method, { chat: CHAT, ...notice });
    await settle();
  };
  const type = async (...keys: string[]) => {
    for (const key of keys) {
      terminal.type(key);
      await settle(key === KEY.escape ? 150 : 60);
    }
  };
  const step = (kind: string, tool = "", args = {}, result = {}, text = "") => notify("chat.step", { kind, tool, args, result, text });
  return { app, sent, notify, type, step, all: () => terminal.all(), last: () => terminal.last(), asked: (method: string) => sent.filter((each) => each.method === method) };
}

test("the interface shows the chat's steps, its answer as Markdown, and the status line", async () => {
  const chat = await open();
  assert.deepEqual(chat.sent[0], { method: "chats.start", params: { text: "Did my deploy succeed?", stay: true } });
  await chat.notify("chat.status", { model: "claude-sonnet-5-5", effort: "medium", mode: "auto", cost: 0.04 });
  await chat.step("user", "", {}, {}, "Did my deploy succeed?");
  await chat.step("tool", "open", { url: "https://vercel.com/leetdavid/uw-site/deployments" }, { tab: 1 });
  let shown = chat.all();
  assert.match(shown, /uw chat · Work · From terminal +3f9c2e18/);
  assert.match(shown, /› Did my deploy succeed\?/);
  assert.match(shown, /✓ Opened vercel\.com\/leetdavid\/uw-site\/deployments +tab 1/);
  assert.match(shown, /Working…/);
  assert.match(shown, /claude-sonnet-5-5 · medium {3}Auto {3}Work {3}\$0\.04/);

  await chat.notify("chat.end", { status: "answered", text: "**No.** The deploy of `uw-site` failed.\n\n### Why\n\n- It missed an import", cost: 0.04 });
  shown = chat.all();
  assert.match(shown, /No\. The deploy of {2}uw-site {2}failed\./);
  assert.match(shown, /· It missed an import/);
  assert.match(chat.last(), /Enter {2}send/);
  assert.doesNotMatch(chat.last(), /Working…/);

  await chat.type(KEY.ctrlC);
  assert.equal(await chat.app.waitUntilExit(), 0);
  assert.match(chat.all(), /Carry on with: uw chat -r 3f9c2e18/);
});

test("memory steps show as counts with the answer, and --no-memory marks the header", async () => {
  const chat = await open({ text: "Find a table near home", memory_off: true });
  await chat.step("user", "", {}, {}, "Find a table near home");
  await chat.step("tool", "search_memory", { words: "home" }, { memories: [{ id: 4, memory: "Lives in the Inner Sunset." }] });
  await chat.step("tool", "remember", { memory: "Partner is vegetarian." }, { kept: { id: 9, memory: "Partner is vegetarian.", core: false } });
  assert.doesNotMatch(chat.all(), /search_memory|remember/);
  await chat.notify("chat.end", { status: "answered", text: "Nopalito has a table.", cost: 0.01 });
  const shown = chat.all();
  assert.match(shown, /uw chat · Work · From terminal · Memory off/);
  assert.match(shown, /Saved 1 memory\s+Recalled 1 memory\s+Nopalito has a table\./);
  await chat.type(KEY.ctrlC);
  await chat.app.waitUntilExit();
});

test("Enter sends the next message, or steers the agent at work, and Esc stops it", async () => {
  const chat = await open({});
  await chat.type("Check the previews too", KEY.enter);
  assert.deepEqual(chat.asked("chats.send"), [{ method: "chats.send", params: { chat: CHAT, text: "Check the previews too" } }]);
  await chat.step("user", "", {}, {}, "Check the previews too");
  assert.match(chat.last(), /Enter {2}steers: it goes in after this step/);
  await chat.type(KEY.escape);
  assert.deepEqual(chat.asked("chats.stop"), [{ method: "chats.stop", params: { chat: CHAT } }]);
  await chat.type(KEY.ctrlO);
  assert.deepEqual(chat.asked("chats.open"), [{ method: "chats.open", params: { chat: CHAT } }]);
  assert.match(chat.all(), /Opened the chat in uw\./);
  await chat.type(KEY.ctrlC);
  assert.equal(await chat.app.waitUntilExit(), 1);
});

test("an approval card asks here: a number answers, arrows pick, and a note declines with it", async () => {
  const chat = await open();
  await chat.notify("chat.approval", { approval: LINEAR });
  let shown = chat.last();
  assert.match(shown, /linear · save_issue/);
  assert.match(shown, /Run save_issue in linear\?/);
  assert.match(shown, /title: Build fails/);
  assert.match(shown, /› 1 {2}Allow once/);
  assert.match(shown, /2 {2}Allow for this chat {2}until this chat ends/);
  assert.match(shown, /3 {2}Always allow {2}in every chat, until you take it back/);
  assert.match(shown, /Tell the agent why, or what to do instead/);
  assert.match(shown, /● Waits for your approval/);
  await chat.type("2");
  assert.deepEqual(chat.asked("chats.approve").at(-1)?.params, { chat: CHAT, choice: "allow-for-chat" });

  await chat.notify("chat.approval", { approval: PAGE });
  shown = chat.last();
  assert.match(shown, /dropbox\.com · tab 1/);
  assert.match(shown, /Click "Yes, cancel Dropbox Plus"\?/);
  assert.match(shown, /▲ Ends your plan on Nov 14\./);
  await chat.type(KEY.down, KEY.enter);
  assert.deepEqual(chat.asked("chats.approve").at(-1)?.params, { chat: CHAT, choice: "decline" });

  await chat.notify("chat.approval", { approval: PAGE });
  await chat.type("Cancel the yearly plan instead", KEY.enter);
  assert.deepEqual(chat.asked("chats.approve").at(-1)?.params, { chat: CHAT, choice: "decline", note: "Cancel the yearly plan instead" });
  await chat.type(KEY.ctrlC);
  assert.equal(await chat.app.waitUntilExit(), 1);
});

test("/ lists skills, then connector prompts, and a prompt takes its arguments in order", async () => {
  const chat = await open({});
  await chat.type("/");
  const shown = chat.last();
  assert.match(shown, /Skills/);
  assert.match(shown, /\/handoff +Only you {2}Write a handoff/);
  assert.match(shown, /Prompts/);
  assert.match(shown, /\/notes:greet <name> +Greets someone\./);
  assert.match(shown, /Tab {2}or {2}Enter {2}picks/);

  await chat.type("n", KEY.tab, "Ada Lovelace", KEY.enter);
  assert.deepEqual(chat.asked("chats.prompt"), [
    { method: "chats.prompt", params: { chat: CHAT, connector: "notes", prompt: "greet", values: { name: "Ada Lovelace" } } },
  ]);
  await chat.type("/notes:greet", KEY.escape, "/notes:greet ", KEY.enter);
  assert.match(chat.all(), /\/notes:greet takes name/);
  await chat.type("/h", KEY.enter);
  assert.deepEqual(chat.asked("chats.send").at(-1)?.params, { chat: CHAT, text: "/handoff" });
  await chat.type(KEY.ctrlC);
  await chat.app.waitUntilExit();
});

const key = (named: Partial<Key> = {}) => ({ ctrl: false, meta: false, backspace: false, delete: false, ...named }) as Key;

test("the composer edits one line", () => {
  let line = edit({ value: "", cursor: 0 }, "helo", key())!;
  line = edit(line, "", key({ leftArrow: true }))!;
  line = edit(line, "l", key())!;
  assert.deepEqual(line, { value: "hello", cursor: 4 });
  line = edit(line, "", key({ backspace: true }))!;
  assert.deepEqual(line, { value: "helo", cursor: 3 });
  assert.deepEqual(edit(line, "a", key({ ctrl: true })), { value: "helo", cursor: 0 });
  assert.equal(edit(line, "o", key({ ctrl: true })), null);
  assert.deepEqual(edit({ value: "", cursor: 0 }, "two\nlines", key()), { value: "two lines", cursor: 9 });
});

test("a prompt's last argument takes the rest of the words", () => {
  const prompt = { connector: "linear", name: "triage", arguments: [{ name: "team", required: true }, { name: "focus", required: false }] };
  assert.deepEqual(promptValues(prompt, "Web bugs from this week"), { values: { team: "Web", focus: "bugs from this week" }, missing: [] });
  assert.deepEqual(promptValues(prompt, ""), { values: {}, missing: ["team"] });
});
