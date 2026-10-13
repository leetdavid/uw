import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { MODES, iconDocument, layerPage } from "../scripts/icon.ts";

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
