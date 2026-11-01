import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { MODES, WINDOWS_SIZES, iconDocument, layerPage, packIco, windowsPage } from "../scripts/icon.ts";

const logo = (mode: string) => readFileSync(new URL(`../docs/assets/logo-${mode}.svg`, import.meta.url), "utf8");

test("both logo files can be cut into a tile layer and a mark layer", () => {
  for (const mode of MODES) {
    const tile = layerPage(logo(mode), "tile");
    const mark = layerPage(logo(mode), "mark");
    assert.match(tile, /#shadow,#ring,#mark\{display:none\}/);
    assert.match(mark, /#shadow,#ring,\.ground\{display:none\}/);
    for (const page of [tile, mark]) {
      assert.match(page, /viewBox="64 64 512 512" width="1024" height="1024"/);
      assert.match(page, /#art\{clip-path:none\}/);
    }
  }
});

test("a logo file without the layer markers is refused", () => {
  const unmarked = logo("light").replace('id="mark"', "");
  assert.throws(() => layerPage(unmarked, "tile"), /lacks id="mark"/);
});

test("the light images show by default and the dark images in the dark appearance", () => {
  const document = JSON.parse(iconDocument()) as {
    groups: { layers: { name: string; "opacity-specializations": { appearance?: string; value: number }[] }[] }[];
  };
  const layers = document.groups.flatMap((group) => group.layers);
  assert.deepEqual(layers.map((layer) => layer.name), ["mark-light", "mark-dark", "tile-light", "tile-dark"]);
  for (const layer of layers) {
    const dark = layer.name.endsWith("-dark");
    assert.deepEqual(layer["opacity-specializations"], [{ value: dark ? 0 : 1 }, { appearance: "dark", value: dark ? 1 : 0 }]);
  }
});

test("the Windows icon is the whole logo as a round-cornered tile at each size", () => {
  const page = windowsPage(logo("light"), 48);
  assert.match(page, /viewBox="64 64 512 512" width="48" height="48"/);
  assert.match(page, /#shadow,#ring\{display:none\}/);
  assert.match(page, /border-radius:22\.5%/);
  assert.throws(() => windowsPage("<svg></svg>", 48), /lacks/);
});

test("an .ico lists each PNG with its size and where it starts", () => {
  const images = WINDOWS_SIZES.slice(-2).map((size) => ({ size, png: Buffer.alloc(size, size % 255) }));
  const ico = packIco(images);
  assert.equal(ico.readUInt16LE(2), 1);
  assert.equal(ico.readUInt16LE(4), 2);
  const first = 6;
  const second = first + 16;
  assert.equal(ico.readUInt8(first), 128);
  assert.equal(ico.readUInt8(second), 0, "256 pixels is written as 0");
  assert.equal(ico.readUInt32LE(first + 8), 128);
  assert.equal(ico.readUInt32LE(first + 12), 6 + 32);
  assert.equal(ico.readUInt32LE(second + 12), 6 + 32 + 128);
  assert.equal(ico.length, 6 + 32 + 128 + 256);
});
