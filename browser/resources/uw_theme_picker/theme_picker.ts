// Copyright 2026 The uw Authors. All rights reserved.

import '/strings.m.js';

import {loadTimeData} from '//resources/js/load_time_data.js';

import type {FieldPoint, GlassSliders, SuggestedTheme, Theme} from './uw_theme_picker.mojom-webui.js';
import {GlassPause, Mode, ThemePickerHandlerFactory, ThemePickerHandlerRemote} from './uw_theme_picker.mojom-webui.js';

const MAX_COLORS = 3;
// The field is pale at the top and full from 52% of its height down, as the
// browser's color field has it.
const PALE_UNTIL = 0.52;
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
const brightness = document.querySelector<HTMLInputElement>('#brightness')!;
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
const autoButton = document.querySelector<HTMLButtonElement>('#mode-auto')!;
const modeButtons = new Map<Mode, HTMLButtonElement>([
  [Mode.kAuto, autoButton],
  [Mode.kLight, document.querySelector<HTMLButtonElement>('#mode-light')!],
  [Mode.kDark, document.querySelector<HTMLButtonElement>('#mode-dark')!],
]);

let theme: Theme;
// Each color as the window shows it, and where it sits on the field.
let shown: number[] = [];
let points: FieldPoint[] = [];
let glassSliders: GlassSliders;
let suggestions: SuggestedTheme[] = [];
let savePending = false;

function css(color: number): string {
  return `#${(color & 0xffffff).toString(16).padStart(6, '0')}`;
}

// Saves at most once a frame while a dot is dragged or a slider moves.
function save() {
  if (savePending) {
    return;
  }
  savePending = true;
  setTimeout(async () => {
    savePending = false;
    const {shown: colors, glass: sliders} = await handler.setTheme(theme);
    shown = colors;
    paintDots();
    showGlass(sliders);
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
      glassSliders.transparencyShown && theme.glass > 0 &&
          glassSliders.pause !== GlassPause.kReduceTransparency);
}

function showGlass(sliders: GlassSliders) {
  glassSliders = sliders;
  frost();
  transparencyRow.hidden = !sliders.transparencyShown;
  glassRow.hidden = !sliders.glassShown;
  transparencyTick.style.left = `calc(${KNOB_RADIUS_PX}px + (100% - ${
      2 * KNOB_RADIUS_PX}px) * ${sliders.readableTransparency / 100})`;
  const why = glassPauses.get(sliders.pause);
  glassWhy.hidden = !sliders.glassShown || !why;
  glassWhy.textContent = why ? loadTimeData.getString(why) : '';
  document.body.classList.toggle(
      'glass-off', sliders.pause === GlassPause.kReduceTransparency);
}

function sameColors(a: number[], b: number[]): boolean {
  return a.length === b.length && a.every((color, i) => color === b[i]);
}

// The dots show each color as the window does, the theme's brightness
// included, and the Brightness track runs from black to the first color.
function paintDots() {
  field.querySelectorAll<HTMLElement>('.dot').forEach((dot, i) => {
    dot.style.background = css(shown[i] ?? theme.colors[i]!);
  });
  brightness.style.setProperty('--first', css(theme.colors[0]!));
}

function placeDot(dot: HTMLElement, point: FieldPoint) {
  dot.style.left = `${point.x * 100}%`;
  dot.style.top = `${point.y * 100}%`;
}

function drawDots() {
  field.querySelectorAll('.dot').forEach(dot => dot.remove());
  theme.colors.forEach((_, index) => {
    const dot = document.createElement('button');
    dot.className = 'dot';
    placeDot(dot, points[index]!);
    dot.setAttribute('aria-label', `${loadTimeData.getString('colorField')} ${
        index + 1}`);
    dot.addEventListener('pointerdown', e => drag(e, dot, index));
    dot.addEventListener('keydown', e => nudge(e, dot, index));
    field.insertBefore(dot, add);
  });
  add.hidden = theme.colors.length >= MAX_COLORS;
  paintDots();
}

function drawSuggestions() {
  suggestedRow.querySelectorAll('button').forEach((button, i) => {
    const suggestion = suggestions[i]!;
    button.setAttribute(
        'aria-pressed',
        String(
            sameColors(suggestion.colors, theme.colors) &&
            suggestion.brightness === theme.brightness));
  });
}

function draw() {
  drawDots();
  drawSuggestions();
  intensity.value = String(theme.intensity);
  intensity.style.setProperty('--value', `${theme.intensity}%`);
  brightness.value = String(theme.brightness);
  grain.value = String(theme.grain);
  transparency.value = String(theme.transparency);
  glass.value = String(theme.glass);
  glass.style.setProperty('--value', `${theme.glass}%`);
}

async function moveDot(dot: HTMLElement, index: number, point: FieldPoint) {
  points[index] = point;
  placeDot(dot, point);
  const {color} = await handler.colorAt(point);
  theme.colors[index] = color;
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
    moveDot(dot, index, {
      x: clamp((e.clientX - box.left) / box.width),
      y: clamp((e.clientY - box.top) / box.height),
    });
  };
  const end = (e: PointerEvent) => {
    dot.removeEventListener('pointermove', move);
    dot.removeEventListener('pointerup', end);
    if (outside(e) && theme.colors.length > 1) {
      theme.colors.splice(index, 1);
      points.splice(index, 1);
      shown.splice(index, 1);
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
  const {x, y} = points[index]!;
  moveDot(dot, index, {
    x: Math.min(1, Math.max(0, x + step[0])),
    y: Math.min(PALE_UNTIL, Math.max(0, y + step[1])),
  });
}

add.addEventListener('click', async () => {
  const point = {
    x: (points[points.length - 1]!.x + 1 / 6) % 1,
    y: PALE_UNTIL / 2,
  };
  const {color} = await handler.colorAt(point);
  theme.colors.push(color);
  points.push(point);
  shown.push(color);
  draw();
  save();
});

intensity.addEventListener('input', () => {
  theme.intensity = Number(intensity.value);
  intensity.style.setProperty('--value', `${theme.intensity}%`);
  save();
});

brightness.addEventListener('input', () => {
  theme.brightness = Number(brightness.value);
  drawSuggestions();
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

function showMode(mode: Mode) {
  for (const [value, button] of modeButtons) {
    button.setAttribute('aria-pressed', String(value === mode));
  }
}

const modeNames = new Map<Mode, string>([
  [Mode.kAuto, 'modeAuto'],
  [Mode.kLight, 'modeLight'],
  [Mode.kDark, 'modeDark'],
]);
for (const [mode, button] of modeButtons) {
  button.querySelector('span')!.textContent =
      loadTimeData.getString(modeNames.get(mode)!);
  button.addEventListener('click', () => {
    showMode(mode);
    handler.setMode(mode);
  });
}
document.querySelector('#mode-label')!.textContent =
    loadTimeData.getString('mode');
autoButton.title = loadTimeData.getString('modeSameAsSystem');
autoButton.setAttribute(
    'aria-label', loadTimeData.getString('modeSameAsSystem'));

field.setAttribute('aria-label', loadTimeData.getString('colorField'));
add.title = loadTimeData.getString('addColor');
add.setAttribute('aria-label', loadTimeData.getString('addColor'));
suggestedRow.setAttribute('aria-label', loadTimeData.getString('suggested'));
document.querySelector('label[for=intensity]')!.textContent =
    loadTimeData.getString('intensity');
document.querySelector('label[for=brightness]')!.textContent =
    loadTimeData.getString('brightness');
document.querySelector('label[for=grain]')!.textContent =
    loadTimeData.getString('grain');
document.querySelector('label[for=transparency]')!.textContent =
    loadTimeData.getString('transparency');
document.querySelector('label[for=glass]')!.textContent =
    loadTimeData.getString('glass');
transparencyTick.title = loadTimeData.getString('glassTick');
transparencyTick.setAttribute(
    'aria-label', loadTimeData.getString('glassTick'));
const {
  theme: saved,
  shown: savedShown,
  points: savedPoints,
  mode,
  suggested,
  glass: sliders,
} = await handler.getState();
theme = saved;
shown = savedShown;
points = savedPoints;
showGlass(sliders);
suggestions = suggested;
suggestedRow.replaceChildren(...suggestions.map((suggestion, i) => {
  const button = document.createElement('button');
  button.style.background = suggestion.shown.length === 1 ?
      css(suggestion.shown[0]!) :
      `linear-gradient(135deg in oklch, ${
          suggestion.shown.map(css).join(', ')})`;
  const name = loadTimeData.getString(`suggestion${i}`);
  button.title = name;
  button.setAttribute('aria-label', name);
  button.addEventListener('click', async () => {
    theme.colors = [...suggestion.colors];
    theme.brightness = suggestion.brightness;
    shown = [...suggestion.shown];
    ({points} = await handler.pointsOf(theme.colors));
    draw();
    save();
  });
  return button;
}));
showMode(mode);
draw();
handler.showUI();
