#!/usr/bin/env node
/** Compile the macOS app icon from the logo SVGs with Xcode's actool. */

import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

const REPO = fileURLToPath(new URL("../", import.meta.url));
const OUTPUT = join(REPO, "browser/branding/mac");
const BROWSER = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const SIZE = 1024;
const DEPLOYMENT_TARGET = "13.0";
const LOGO_VIEW = 'viewBox="0 0 640 640" width="640" height="640"';
const TILE_VIEW = `viewBox="64 64 512 512" width="${SIZE}" height="${SIZE}"`;
const MARKERS = [LOGO_VIEW, 'id="shadow"', 'id="art"', 'class="ground"', 'id="mark"', 'id="ring"'];
const HIDDEN = { tile: "#mark", mark: ".ground" } as const;

export const MODES = ["light", "dark"] as const;
export type Mode = (typeof MODES)[number];
export type Layer = keyof typeof HIDDEN;

/** A page that shows one layer of a logo file, cropped to the tile and unclipped. */
export function layerPage(logo: string, layer: Layer): string {
  const missing = MARKERS.filter((marker) => !logo.includes(marker));
  if (missing.length) throw new Error(`The logo file lacks ${missing.join(", ")}. The icon is cut along these markers.`);
  return "<!doctype html><style>html,body{margin:0;background:transparent}svg{display:block}" +
    `#shadow,#ring,${HIDDEN[layer]}{display:none}#art{clip-path:none}</style>${logo.replace(LOGO_VIEW, TILE_VIEW)}`;
}

function shownIn(mode: Mode) {
  return [{ value: mode === "light" ? 1 : 0 }, { appearance: "dark", value: mode === "dark" ? 1 : 0 }];
}

function group(layer: Layer) {
  return {
    layers: MODES.map((mode) => ({
      glass: false, "image-name": `${layer}-${mode}.png`, name: `${layer}-${mode}`, "opacity-specializations": shownIn(mode),
    })),
    shadow: { kind: "none", opacity: 0.5 },
    specular: false,
    translucency: { enabled: false, value: 0.5 },
  };
}

/** The Icon Composer document: the mark above the tile, each with a light and a dark image. */
export function iconDocument(): string {
  return JSON.stringify({
    fill: { solid: "extended-gray:1.00000,1.00000" },
    groups: [group("mark"), group("tile")],
    "supported-platforms": { squares: "shared" },
  }, null, 2);
}

function run(command: readonly string[], task: string): string {
  const result = spawnSync(command[0]!, command.slice(1), { encoding: "utf8", timeout: 180_000 });
  if (result.status !== 0) throw new Error(`${task} failed.\n${result.stderr || result.stdout || result.error?.message || ""}`);
  return result.stdout;
}

function render(browser: string, work: string, name: string, page: string): void {
  const html = join(work, `${name}.html`);
  const image = join(work, "AppIcon.icon/Assets", `${name}.png`);
  writeFileSync(html, page);
  run([
    browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run", `--user-data-dir=${join(work, "profile")}`,
    "--force-device-scale-factor=1", `--window-size=${SIZE},${SIZE}`, "--default-background-color=00000000",
    `--screenshot=${image}`, pathToFileURL(html).href,
  ], `Rendering ${name}`);
  if (!existsSync(image)) throw new Error(`${browser} wrote no image for ${name}.`);
}

function compile(work: string): string {
  const compiled = join(work, "compiled");
  mkdirSync(compiled);
  const report = run([
    "xcrun", "actool", join(work, "AppIcon.icon"), "--compile", compiled, "--platform", "macosx",
    "--minimum-deployment-target", DEPLOYMENT_TARGET, "--app-icon", "AppIcon",
    "--output-partial-info-plist", join(work, "partial.plist"), "--errors", "--warnings", "--output-format", "human-readable-text",
  ], "Compiling the icon");
  for (const name of ["Assets.car", "AppIcon.icns"]) {
    if (!existsSync(join(compiled, name))) throw new Error(`actool wrote no ${name}.\n${report}`);
  }
  return compiled;
}

export function buildIcon(browser = BROWSER, output = OUTPUT): void {
  if (!existsSync(browser)) throw new Error(`${browser} is missing. Pass --browser with the path of a Chromium-based browser.`);
  const work = mkdtempSync(join(tmpdir(), "uw-icon-"));
  try {
    mkdirSync(join(work, "AppIcon.icon/Assets"), { recursive: true });
    for (const mode of MODES) {
      const logo = readFileSync(join(REPO, `docs/assets/logo-${mode}.svg`), "utf8");
      for (const layer of ["tile", "mark"] as const) render(browser, work, `${layer}-${mode}`, layerPage(logo, layer));
    }
    writeFileSync(join(work, "AppIcon.icon/icon.json"), iconDocument());
    const compiled = compile(work);
    mkdirSync(output, { recursive: true });
    copyFileSync(join(compiled, "Assets.car"), join(output, "Assets.car"));
    copyFileSync(join(compiled, "AppIcon.icns"), join(output, "app.icns"));
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
}

export function main(argv = process.argv.slice(2)): number {
  try {
    const { values } = parseArgs({ args: argv, strict: true, allowPositionals: false, options: { browser: { type: "string" } } });
    buildIcon(values.browser);
    console.log(`Wrote Assets.car and app.icns to ${OUTPUT}`);
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    return 1;
  }
}

if (import.meta.main) process.exitCode = main();
