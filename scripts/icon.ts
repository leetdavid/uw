#!/usr/bin/env node
/** Compile the app icons from the logo SVGs: macOS's with Xcode's actool, and Windows' .ico. */

import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

const REPO = fileURLToPath(new URL("../", import.meta.url));
const OUTPUT = join(REPO, "browser/branding/mac");
const WINDOWS_OUTPUT = join(REPO, "browser/branding/win");
const BROWSER = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const SIZE = 1024;
const DEPLOYMENT_TARGET = "13.0";
const LOGO_VIEW = 'viewBox="0 0 640 640" width="640" height="640"';
const TILE_VIEW = `viewBox="64 64 512 512" width="${SIZE}" height="${SIZE}"`;
const MARKERS = [LOGO_VIEW, 'id="shadow"', 'id="art"', 'class="ground"', 'id="mark"', 'id="ring"'];
const HIDDEN = { tile: "#mark", mark: ".ground" } as const;
// Windows picks the closest size for each place the icon shows.
export const WINDOWS_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256] as const;

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

/** A page that shows the whole light logo at `size` pixels, as a tile with round corners. macOS rounds
 * its icons itself; Windows shows them as drawn. */
export function windowsPage(logo: string, size: number): string {
  const missing = MARKERS.filter((marker) => !logo.includes(marker));
  if (missing.length) throw new Error(`The logo file lacks ${missing.join(", ")}. The icon is cut along these markers.`);
  return "<!doctype html><style>html,body{margin:0;background:transparent}svg{display:block;border-radius:22.5%}" +
    `#shadow,#ring{display:none}#art{clip-path:none}</style>${logo.replace(LOGO_VIEW, `viewBox="64 64 512 512" width="${size}" height="${size}"`)}`;
}

/** An .ico holding each image as a PNG, which Windows reads since Vista. */
export function packIco(images: readonly { size: number; png: Buffer }[]): Buffer {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(images.length, 4);
  const entries = Buffer.alloc(16 * images.length);
  let offset = header.length + entries.length;
  images.forEach(({ size, png }, index) => {
    const at = index * 16;
    entries.writeUInt8(size >= 256 ? 0 : size, at);
    entries.writeUInt8(size >= 256 ? 0 : size, at + 1);
    entries.writeUInt16LE(1, at + 4);
    entries.writeUInt16LE(32, at + 6);
    entries.writeUInt32LE(png.length, at + 8);
    entries.writeUInt32LE(offset, at + 12);
    offset += png.length;
  });
  return Buffer.concat([header, entries, ...images.map(({ png }) => png)]);
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

function render(browser: string, work: string, name: string, page: string, size = SIZE,
  image = join(work, "AppIcon.icon/Assets", `${name}.png`)): void {
  const html = join(work, `${name}.html`);
  writeFileSync(html, page);
  run([
    browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run", `--user-data-dir=${join(work, "profile")}`,
    "--force-device-scale-factor=1", `--window-size=${size},${size}`, "--default-background-color=00000000",
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

export function buildWindowsIcon(browser = BROWSER, output = WINDOWS_OUTPUT): void {
  if (!existsSync(browser)) throw new Error(`${browser} is missing. Pass --browser with the path of a Chromium-based browser.`);
  const work = mkdtempSync(join(tmpdir(), "uw-icon-"));
  try {
    const logo = readFileSync(join(REPO, "docs/assets/logo-light.svg"), "utf8");
    const images = WINDOWS_SIZES.map((size) => {
      const image = join(work, `windows-${size}.png`);
      render(browser, work, `windows-${size}`, windowsPage(logo, size), size, image);
      return { size, png: readFileSync(image) };
    });
    mkdirSync(output, { recursive: true });
    writeFileSync(join(output, "uw.ico"), packIco(images));
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
}

export function main(argv = process.argv.slice(2)): number {
  try {
    const { values } = parseArgs({
      args: argv, strict: true, allowPositionals: false,
      options: { browser: { type: "string" }, windows: { type: "boolean" } },
    });
    if (!values.windows) {
      buildIcon(values.browser);
      console.log(`Wrote Assets.car and app.icns to ${OUTPUT}`);
    }
    buildWindowsIcon(values.browser);
    console.log(`Wrote uw.ico to ${WINDOWS_OUTPUT}`);
    return 0;
  } catch (error) {
    console.error(`Error: ${error instanceof Error ? error.message : String(error)}`);
    return 1;
  }
}

if (import.meta.main) process.exitCode = main();
