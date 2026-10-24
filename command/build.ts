// Builds the uw command into one program for uw.app/Contents/Helpers/uw, per
// docs/adr/0016-the-uw-command-is-typescript-with-ink.md: bundles src/ with
// esbuild, then puts the bundle into a copy of the node that runs this script,
// as Node's single executable applications do. GN runs it with Chromium's node:
//   build.ts --out <program> --version <uw's version> --depfile <file>
import { execFileSync } from "node:child_process";
import { chmodSync, copyFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import * as esbuild from "esbuild";
import { inject } from "postject";

const SEA_FUSE = "NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2";

const here = dirname(fileURLToPath(import.meta.url));
const { values } = parseArgs({ options: { out: { type: "string" }, version: { type: "string" }, depfile: { type: "string" } } });
if (!values.out || !values.version || !values.depfile) {
  throw new Error("Usage: build.ts --out <program> --version <version> --depfile <file>");
}
const program = resolve(values.out);
const work = `${program}.parts`;
mkdirSync(work, { recursive: true });

const bundle = join(work, "uw.cjs");
const built = await esbuild.build({
  entryPoints: [join(here, "src/main.ts")],
  outfile: bundle,
  bundle: true,
  platform: "node",
  format: "cjs",
  target: "node24",
  define: { UW_VERSION: JSON.stringify(values.version) },
  metafile: true,
  logLevel: "warning",
});

const blob = join(work, "uw.blob");
const config = join(work, "sea-config.json");
writeFileSync(config, JSON.stringify({ main: bundle, output: blob, disableExperimentalSEAWarning: true }));
execFileSync(process.execPath, ["--experimental-sea-config", config], { stdio: "inherit" });

copyFileSync(process.execPath, program);
chmodSync(program, 0o755);
execFileSync("codesign", ["--remove-signature", program]);
await inject(program, "NODE_SEA_BLOB", readFileSync(blob), { sentinelFuse: SEA_FUSE, machoSegmentName: "NODE_SEA" });
execFileSync("codesign", ["--sign", "-", "--force", program]);

const inputs = [import.meta.filename, ...Object.keys(built.metafile.inputs).map((input) => resolve(input))];
mkdirSync(dirname(resolve(values.depfile)), { recursive: true });
writeFileSync(values.depfile, `${relative(process.cwd(), program)}: ${inputs.join(" ")}\n`);
