#!/usr/bin/env node
/** End-to-end crash verification: native build, smoke, focused test suites.
 *
 * Runs the native crash-focused sequence in one command and fails fast with
 * crash-report pointers. Run `pnpm run check` first for TypeScript checking
 * and tooling tests. Focused suites pin every fixed native crash; see
 * docs/try-uw.md#crash-regression-checklist for the manual remainder and
 * docs/plans/automatic-organization.md for provider-accuracy checks, which
 * need a configured endpoint and stay manual.
 */

import { join } from "node:path";
import { parseArgs } from "node:util";
import { BuildError, Chromium, REPO, layoutAt, loadPins, nativeRuntime } from "./chromium.ts";

const OUTPUT = "out/uw";
const SMOKE_TIMEOUT = 120_000;
const UNIT_FILTER = [
  "TabTreeTest.*",
  "OrganizationCoordinatorTest.*",
  "AiProtocolTest.*",
  "VerticalTabStripPrefsTest.*",
  "VerticalTabStripStateControllerTest.*",
].join(":");
const BROWSER_FILTER = "TabOrganizationControllerBrowserTest.*";

function positiveInteger(value: string, flag: string): number {
  const number = Number(value);
  if (!/^\d+$/.test(value) || !Number.isSafeInteger(number) || number < 1) {
    throw new BuildError(`${flag} must be a positive integer`, 2);
  }
  return number;
}

export function main(argv = process.argv.slice(2)): number {
  try {
    const { values } = parseArgs({
      args: argv, strict: true, allowPositionals: false,
      options: {
        help: { type: "boolean", short: "h" }, "checkout-dir": { type: "string" },
        jobs: { type: "string" as const },
      },
    });
    if (values.help) {
      console.log("End-to-end crash verification: build, smoke, native suites.\n\n" +
        "Usage: node scripts/verify.ts [--checkout-dir PATH] [--jobs NUMBER]\n\n" +
        "Examples:\n  node scripts/verify.ts\n  node scripts/verify.ts --jobs 2\n");
      return 0;
    }
    const jobs = positiveInteger(String(values.jobs ?? "8"), "--jobs");
    const layout = layoutAt(String(values["checkout-dir"] ??
      (process.env.UW_CHROMIUM_ROOT || join(REPO, ".chromium"))));
    const chromium = new Chromium(layout, loadPins(), nativeRuntime());
    chromium.build(jobs);
    chromium.run(
      [join(layout.tools, "autoninja"), "-C", OUTPUT, `-j${jobs}`, "unit_tests", "browser_tests"],
      layout.source);
    chromium.smoke(SMOKE_TIMEOUT);
    for (const [binary, filter] of [
      [join(layout.source, OUTPUT, "unit_tests"), UNIT_FILTER],
      [join(layout.source, OUTPUT, "browser_tests"), BROWSER_FILTER],
    ] as const) {
      chromium.run([binary, `--gtest_filter=${filter}`], layout.source);
    }
    chromium.runtime.log("VERIFY PASS: build, smoke, and native crash suites");
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    if (error instanceof BuildError) return error.exitCode;
    return 1;
  }
}

if (import.meta.main) process.exitCode = main();
