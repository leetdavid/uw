#!/usr/bin/env node
/** The uw command. See docs/plans/uw-command.md. */

import { run } from "./cli.ts";
import { place } from "./launch.ts";

process.exitCode = await run(process.argv.slice(2), {
  out: (text) => process.stdout.write(text),
  err: (text) => process.stderr.write(text),
  place: place(),
});
