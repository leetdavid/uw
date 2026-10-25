#!/usr/bin/env node
/** The uw command. See docs/plans/uw-command.md. */

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
}).then((exit) => {
  process.exitCode = exit;
});
