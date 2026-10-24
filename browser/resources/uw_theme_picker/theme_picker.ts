// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import '/strings.m.js';

import {loadTimeData} from '//resources/js/load_time_data.js';

import type {GlassSliders, Theme} from './uw_theme_picker.mojom-webui.js';
import {ColorMode, GlassPause, PageMode, ThemePickerHandlerFactory, ThemePickerHandlerRemote} from './uw_theme_picker.mojom-webui.js';

const MAX_COLORS = 3;
// The field is pale at the top and full from 52% of its height down.
const PALE_UNTIL = 0.52;
const PALEST = 0.92;
const FULL_SATURATION = 0.62;
const FULL_LIGHTNESS = 0.7;
// A dot let go this far outside the field is removed.
const REMOVE_DISTANCE = 24;
const KEY_STEP = 0.02;
const SAVE_DELAY_MS = 16;
// Half the slider knob, which the tick lines up with.
const KNOB_RADIUS_PX = 7;

const handler = new ThemePickerHandlerRemote();
ThemePickerHandlerFactory.getRemote().createHandler(
    handler.$.bindNewPipeAndPassReceiver());

const field = document.querySelector<HTMLElement>('#field')!;
const add = document.querySelector<HTMLButtonElement>('#add')!;
const suggestedRow = document.querySelector<HTMLElement>('#suggested')!;
const intensity = document.querySelector<HTMLInputElement>('#intensity')!;
const grain = document.querySelector<HTMLInputElement>('#grain')!;
const transparencyRow =
    document.querySelector<HTMLElement>('#transparency-row')!;
const transparency =
    document.querySelector<HTMLInputElement>('#transparency')!;
const transparencyTick =
    document.querySelector<HTMLElement>('#transparency-tick')!;
const glassRow = document.querySelector<HTMLElement>('#glass-row')!;
const glass = document.querySelector<HTMLInputElement>('#glass')!;
const glassWhy = document.querySelector<HTMLElement>('#glass-why')!;
const colorsInDarkMode =
    document.querySelector<HTMLInputElement>('#colors-in-dark-mode')!;
const modes = new Map<ColorMode, HTMLButtonElement>([
  [ColorMode.kAuto, document.querySelector<HTMLButtonElement>('#auto')!],
  [ColorMode.kLight, document.querySelector<HTMLButtonElement>('#light')!],
  [ColorMode.kDark, document.querySelector<HTMLButtonElement>('#dark')!],
]);
const pagesWindow =
    document.querySelector<HTMLButtonElement>('#pages-window')!;
const pageModes = new Map<PageMode, HTMLButtonElement>([
  [PageMode.kWindow, pagesWindow],
  [
    PageMode.kLight,
    document.querySelector<HTMLButtonElement>('#pages-light')!,
  ],
  [PageMode.kDark, document.querySelector<HTMLButtonElement>('#pages-dark')!],
]);

let theme: Theme;
let currentMode: ColorMode;
let glassSliders: GlassSliders;
let suggestions: number[][] = [];
let savePending = false;

function css(color: number): string {
  return `#${(color & 0xffffff).toString(16).padStart(6, '0')}`;
}

function hslToColor(h: number, s: number, l: number): number {
  const k = (n: number) => (n + h * 12) % 12;
  const a = s * Math.min(l, 1 - l);
  const channel = (n: number) => Math.round(
      255 * (l - a * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1))));
  return (0xff000000 | (channel(0) << 16) | (channel(8) << 8) | channel(4)) >>>
      0;
}

function colorToHsl(color: number): [number, number, number] {
  const r = ((color >> 16) & 0xff) / 255;
  const g = ((color >> 8) & 0xff) / 255;
  const b = (color & 0xff) / 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) {
    return [0, 0, l];
  }
  const d = max - min;
  const s = d / (1 - Math.abs(2 * l - 1));
  const h = max === r ? ((g - b) / d + 6) % 6 :
      max === g       ? (b - r) / d + 2 :
                        (r - g) / d + 4;
  return [h / 6, s, l];
}

// The color under a point of the field, each axis from 0 to 1: the hue runs
// left to right, pale at the top to full at the middle.
function colorAt(x: number, y: number): number {
  const paleness = Math.max(0, 1 - y / PALE_UNTIL) * PALEST;
  return hslToColor(
      x, FULL_SATURATION * (1 - paleness),
      FULL_LIGHTNESS + (1 - FULL_LIGHTNESS) * paleness);
}

function placeOf(color: number): [number, number] {
  const [h, , l] = colorToHsl(color);
  const paleness = Math.min(
      PALEST, Math.max(0, (l - FULL_LIGHTNESS) / (1 - FULL_LIGHTNESS)));
  return [h, (1 - paleness / PALEST) * PALE_UNTIL];
}

// Saves at most once a frame while a dot is dragged or a slider moves.
function save() {
  if (savePending) {
    return;
  }
  savePending = true;
  setTimeout(() => {
    savePending = false;
    handler.setTheme(theme);
    refreshGlass();
  }, SAVE_DELAY_MS);
}

const glassPauses = new Map<GlassPause, string>([
  [GlassPause.kEnergySaver, 'glassEnergySaver'],
  [GlassPause.kReduceTransparency, 'glassReduceTransparency'],
]);

// The picker is frosted glass while the theme's glass is on.
function frost() {
  document.body.classList.toggle(
      'frosted',
      glassSliders.shown && theme.glass > 0 &&
          glassSliders.pause !== GlassPause.kReduceTransparency);
}

function showGlass(sliders: GlassSliders) {
  glassSliders = sliders;
  frost();
  transparencyRow.hidden = !sliders.shown;
  glassRow.hidden = !sliders.shown;
  transparencyTick.style.left = `calc(${KNOB_RADIUS_PX}px + (100% - ${
      2 * KNOB_RADIUS_PX}px) * ${sliders.readableTransparency / 100})`;
  const why = glassPauses.get(sliders.pause);
  glassWhy.hidden = !sliders.shown || !why;
  glassWhy.textContent = why ? loadTimeData.getString(why) : '';
  document.body.classList.toggle(
      'glass-off', sliders.pause === GlassPause.kReduceTransparency);
}

// The tick moves with the theme's colors and mode.
async function refreshGlass() {
  const {glass: sliders} =
      await handler.getGlassSliders(theme, currentMode);
  showGlass(sliders);
}

function sameColors(a: number[], b: number[]): boolean {
  return a.length === b.length && a.every((color, i) => color === b[i]);
}

function drawDots() {
  field.querySelectorAll('.dot').forEach(dot => dot.remove());
  theme.colors.forEach((color, index) => {
    const [x, y] = placeOf(color);
    const dot = document.createElement('button');
    dot.className = 'dot';
    dot.style.background = css(color);
    dot.style.left = `${x * 100}%`;
    dot.style.top = `${y * 100}%`;
    dot.setAttribute('aria-label', `${loadTimeData.getString('colorField')} ${
        index + 1}`);
    dot.addEventListener('pointerdown', e => drag(e, dot, index));
    dot.addEventListener('keydown', e => nudge(e, dot, index));
    field.insertBefore(dot, add);
  });
  add.hidden = theme.colors.length >= MAX_COLORS;
}

function drawSuggestions() {
  suggestedRow.querySelectorAll('button').forEach((button, i) => {
    button.setAttribute(
        'aria-pressed', String(sameColors(suggestions[i]!, theme.colors)));
  });
}

function draw() {
  drawDots();
  drawSuggestions();
  intensity.value = String(theme.intensity);
  intensity.style.setProperty('--value', `${theme.intensity}%`);
  grain.value = String(theme.grain);
  transparency.value = String(theme.transparency);
  glass.value = String(theme.glass);
  glass.style.setProperty('--value', `${theme.glass}%`);
  colorsInDarkMode.checked = theme.colorsInDarkMode;
}

function moveDot(dot: HTMLElement, index: number, x: number, y: number) {
  const color = colorAt(x, y);
  theme.colors[index] = color;
  dot.style.background = css(color);
  dot.style.left = `${x * 100}%`;
  dot.style.top = `${y * 100}%`;
  drawSuggestions();
  save();
}

function drag(start: PointerEvent, dot: HTMLElement, index: number) {
  dot.setPointerCapture(start.pointerId);
  const box = field.getBoundingClientRect();
  const clamp = (value: number) => Math.min(1, Math.max(0, value));
  const outside = (e: PointerEvent) => e.clientX < box.left - REMOVE_DISTANCE ||
      e.clientX > box.right + REMOVE_DISTANCE ||
      e.clientY < box.top - REMOVE_DISTANCE ||
      e.clientY > box.bottom + REMOVE_DISTANCE;
  const move = (e: PointerEvent) => {
    moveDot(
        dot, index, clamp((e.clientX - box.left) / box.width),
        clamp((e.clientY - box.top) / box.height));
  };
  const end = (e: PointerEvent) => {
    dot.removeEventListener('pointermove', move);
    dot.removeEventListener('pointerup', end);
    if (outside(e) && theme.colors.length > 1) {
      theme.colors.splice(index, 1);
      draw();
      save();
    }
  };
  dot.addEventListener('pointermove', move);
  dot.addEventListener('pointerup', end);
}

function nudge(e: KeyboardEvent, dot: HTMLElement, index: number) {
  const steps: Record<string, [number, number]> = {
    ArrowLeft: [-KEY_STEP, 0],
    ArrowRight: [KEY_STEP, 0],
    ArrowUp: [0, -KEY_STEP],
    ArrowDown: [0, KEY_STEP],
  };
  const step = steps[e.key];
  if (!step) {
    return;
  }
  e.preventDefault();
  const [x, y] = placeOf(theme.colors[index]!);
  moveDot(
      dot, index, Math.min(1, Math.max(0, x + step[0])),
      Math.min(PALE_UNTIL, Math.max(0, y + step[1])));
}

add.addEventListener('click', () => {
  const [x] = placeOf(theme.colors[theme.colors.length - 1]!);
  theme.colors.push(colorAt((x + 1 / 6) % 1, PALE_UNTIL / 2));
  draw();
  save();
});

intensity.addEventListener('input', () => {
  theme.intensity = Number(intensity.value);
  intensity.style.setProperty('--value', `${theme.intensity}%`);
  save();
});

grain.addEventListener('input', () => {
  theme.grain = Number(grain.value);
  save();
});

transparency.addEventListener('input', () => {
  theme.transparency = Number(transparency.value);
  save();
});

glass.addEventListener('input', () => {
  theme.glass = Number(glass.value);
  glass.style.setProperty('--value', `${theme.glass}%`);
  frost();
  save();
});

colorsInDarkMode.addEventListener('change', () => {
  theme.colorsInDarkMode = colorsInDarkMode.checked;
  save();
});

function showMode(mode: ColorMode) {
  for (const [value, button] of modes) {
    button.setAttribute('aria-pressed', String(value === mode));
  }
}

const modeNames = new Map<ColorMode, string>([
  [ColorMode.kAuto, 'modeAuto'],
  [ColorMode.kLight, 'modeLight'],
  [ColorMode.kDark, 'modeDark'],
]);
for (const [mode, button] of modes) {
  button.querySelector('span')!.textContent =
      loadTimeData.getString(modeNames.get(mode)!);
  button.addEventListener('click', () => {
    currentMode = mode;
    showMode(mode);
    handler.setMode(mode);
    refreshGlass();
  });
}

function showPageMode(mode: PageMode) {
  for (const [value, button] of pageModes) {
    button.setAttribute('aria-pressed', String(value === mode));
  }
}

const pageModeNames = new Map<PageMode, string>([
  [PageMode.kWindow, 'pagesWindow'],
  [PageMode.kLight, 'modeLight'],
  [PageMode.kDark, 'modeDark'],
]);
for (const [mode, button] of pageModes) {
  button.querySelector('span')!.textContent =
      loadTimeData.getString(pageModeNames.get(mode)!);
  button.addEventListener('click', () => {
    showPageMode(mode);
    handler.setPageMode(mode);
  });
}
document.querySelector('#pages-label')!.textContent =
    loadTimeData.getString('pages');
pagesWindow.title = loadTimeData.getString('pagesSameAsWindow');
pagesWindow.setAttribute(
    'aria-label', loadTimeData.getString('pagesSameAsWindow'));

field.setAttribute('aria-label', loadTimeData.getString('colorField'));
add.title = loadTimeData.getString('addColor');
add.setAttribute('aria-label', loadTimeData.getString('addColor'));
suggestedRow.setAttribute('aria-label', loadTimeData.getString('suggested'));
document.querySelector('label[for=intensity]')!.textContent =
    loadTimeData.getString('intensity');
document.querySelector('label[for=grain]')!.textContent =
    loadTimeData.getString('grain');
document.querySelector('label[for=transparency]')!.textContent =
    loadTimeData.getString('transparency');
document.querySelector('label[for=glass]')!.textContent =
    loadTimeData.getString('glass');
transparencyTick.title = loadTimeData.getString('glassTick');
transparencyTick.setAttribute(
    'aria-label', loadTimeData.getString('glassTick'));
document.querySelector('label[for=colors-in-dark-mode]')!.textContent =
    loadTimeData.getString('colorsInDarkMode');

const {theme: saved, mode, pageMode, suggested, glass: sliders} =
    await handler.getState();
theme = saved;
currentMode = mode;
showGlass(sliders);
suggestions = suggested;
suggestedRow.replaceChildren(...suggestions.map((colors, i) => {
  const button = document.createElement('button');
  button.style.background = colors.length === 1 ?
      css(colors[0]!) :
      `linear-gradient(135deg, ${colors.map(css).join(', ')})`;
  const name = loadTimeData.getString(`suggestion${i}`);
  button.title = name;
  button.setAttribute('aria-label', name);
  button.addEventListener('click', () => {
    theme.colors = [...colors];
    draw();
    save();
  });
  return button;
}));
showMode(mode);
showPageMode(pageMode);
draw();
handler.showUI();
