// Builds the uw command into one program for uw.app/Contents/Helpers/uw, per
// docs/adr/0016-the-uw-command-is-typescript-with-ink.md: bundles src/ with
// esbuild, then puts the bundle into a copy of the node that runs this script,
// as Node's single executable applications do. Ink loads with top-level await,
// which only an ES module has, and a single executable starts a CommonJS
// script, so the bundle is an asset that a small starter imports. GN runs it
// with Chromium's node:
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

const bundle = join(work, "uw.mjs");
const built = await esbuild.build({
  entryPoints: [join(here, "src/main.ts")],
  outfile: bundle,
  bundle: true,
  platform: "node",
  format: "esm",
  // A smaller bundle starts faster.
  minify: true,
  target: "node24",
  define: { UW_VERSION: JSON.stringify(values.version), "process.env.NODE_ENV": JSON.stringify("production") },
  // Ink loads React devtools only when they are installed, for its own development.
  plugins: [{
    name: "no-devtools",
    setup: (build) => {
      build.onResolve({ filter: /^react-devtools-core$/ }, () => ({ path: "react-devtools-core", namespace: "no-devtools" }));
      build.onLoad({ filter: /.*/, namespace: "no-devtools" }, () => ({ contents: "export default {};" }));
    },
  }],
  // Packages written as CommonJS require Node's own modules.
  banner: { js: 'import { createRequire } from "node:module"; const require = createRequire(process.execPath);' },
  metafile: true,
  logLevel: "warning",
});

const starter = join(work, "start.cjs");
writeFileSync(starter, `const { getAsset } = require("node:sea");
import(\`data:text/javascript;base64,\${Buffer.from(getAsset("uw.mjs")).toString("base64")}\`);
`);
const blob = join(work, "uw.blob");
const config = join(work, "sea-config.json");
writeFileSync(config, JSON.stringify({
  main: starter, output: blob, assets: { "uw.mjs": bundle }, disableExperimentalSEAWarning: true,
}));
execFileSync(process.execPath, ["--experimental-sea-config", config], { stdio: "inherit" });

copyFileSync(process.execPath, program);
chmodSync(program, 0o755);
execFileSync("codesign", ["--remove-signature", program]);
await inject(program, "NODE_SEA_BLOB", readFileSync(blob), { sentinelFuse: SEA_FUSE, machoSegmentName: "NODE_SEA" });
execFileSync("codesign", ["--sign", "-", "--force", program]);

// Inputs from a plugin, such as "no-devtools:react-devtools-core", are no files.
const files = Object.keys(built.metafile.inputs).filter((input) => !input.includes(":"));
const inputs = [import.meta.filename, ...files.map((input) => resolve(input))];
mkdirSync(dirname(resolve(values.depfile)), { recursive: true });
writeFileSync(values.depfile, `${relative(process.cwd(), program)}: ${inputs.join(" ")}\n`);
