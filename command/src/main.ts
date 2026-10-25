#!/usr/bin/env node
/** The uw command. See docs/plans/uw-command.md. */

import { createInterface } from "node:readline/promises";
import { text as readAll } from "node:stream/consumers";
import { run } from "./cli.ts";
import { place } from "./launch.ts";

void run(process.argv.slice(2), {
  out: (text) => process.stdout.write(text),
  err: (text) => process.stderr.write(text),
  place: place(),
  env: process.env,
  terminal: Boolean(process.stdin.isTTY && process.stderr.isTTY),
  stdin: () => readAll(process.stdin),
  ask: async (prompt) => {
    const lines = createInterface({ input: process.stdin, output: process.stderr });
    try {
      return await lines.question(prompt);
    } finally {
      lines.close();
    }
  },
  onInterrupt: (stopped) => process.once("SIGINT", stopped),
  ...(process.stdin.isTTY && process.stdout.isTTY ? { screen: { stdin: process.stdin, stdout: process.stdout } } : {}),
  pipes: { stdin: process.stdin, stdout: process.stdout },
}).then((exit) => {
  process.exitCode = exit;
});
