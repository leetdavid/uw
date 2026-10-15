// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {FitAddon, init, Terminal} from '/ghostty-web.js';
import type {ITheme} from '/ghostty-web.js';

import {TerminalHandlerFactory, TerminalHandlerRemote, TerminalPageCallbackRouter} from './uw_terminal.mojom-webui.js';

// The page card's colors in light and dark mode, with the task colors as the
// shell's eight.
const LIGHT: ITheme = {
  background: '#ffffff',
  foreground: '#1e1b2e',
  cursor: '#3b5bdb',
  cursorAccent: '#ffffff',
  selectionBackground: '#d5dcf8',
  selectionForeground: '#1e1b2e',
  black: '#1e1b2e',
  red: '#c5221f',
  green: '#188038',
  yellow: '#9a6700',
  blue: '#1a73e8',
  magenta: '#9334e6',
  cyan: '#007b83',
  white: '#77748c',
  brightBlack: '#4a4760',
  brightRed: '#d93025',
  brightGreen: '#1e8e3e',
  brightYellow: '#b06000',
  brightBlue: '#3b5bdb',
  brightMagenta: '#a142f4',
  brightCyan: '#12939b',
  brightWhite: '#b9b5cc',
};

const DARK: ITheme = {
  background: '#0a0a0a',
  foreground: '#ededed',
  cursor: '#9db0ff',
  cursorAccent: '#0a0a0a',
  selectionBackground: '#333333',
  selectionForeground: '#ededed',
  black: '#1a1a1a',
  red: '#f28b82',
  green: '#81c995',
  yellow: '#fdd663',
  blue: '#8ab4f8',
  magenta: '#c58af9',
  cyan: '#78d9ec',
  white: '#a1a1a1',
  brightBlack: '#737373',
  brightRed: '#f6aea9',
  brightGreen: '#a8dab5',
  brightYellow: '#fde293',
  brightBlue: '#aecbfa',
  brightMagenta: '#d7aefb',
  brightCyan: '#a1e4f2',
  brightWhite: '#ffffff',
};

// ghostty-web takes its scrollback limit in bytes. This is Ghostty's default.
const SCROLLBACK_BYTES = 10_000_000;

// The Keyboard Map API, which the TypeScript library does not describe yet.
interface KeyboardLayoutMap {
  get(code: string): string|undefined;
}
declare global {
  interface Navigator {
    keyboard?: {getLayoutMap(): Promise<KeyboardLayoutMap>};
  }
}

const handler = new TerminalHandlerRemote();
const page = new TerminalPageCallbackRouter();
TerminalHandlerFactory.getRemote().createHandler(
    page.$.bindNewPipeAndPassRemote(), handler.$.bindNewPipeAndPassReceiver());

await init();
const terminal = new Terminal({
  cursorBlink: true,
  fontFamily: 'ui-monospace, "SF Mono", Menlo, monospace',
  fontSize: 13,
  scrollback: SCROLLBACK_BYTES,
  // ghostty-web cannot change colors once it has opened.
  theme: window.matchMedia('(prefers-color-scheme: dark)').matches ? DARK :
                                                                   LIGHT,
});
const fit = new FitAddon();
terminal.loadAddon(fit);
terminal.open(document.querySelector<HTMLElement>('#terminal')!);
fit.fit();
fit.observeResize();

// The tab is named after what the shell sets, otherwise after its folder.
let folder = '';
let shellTitle = '';
function showTitle() {
  const title = shellTitle || folder;
  if (title) {
    document.title = title;
  }
}
terminal.onTitleChange((title: string) => {
  shellTitle = title;
  showTitle();
});
page.setFolder.addListener((name: string) => {
  folder = name;
  showTitle();
});

page.output.addListener((text: string) => {
  // ghostty-web throws on an empty write.
  if (text) {
    terminal.write(text);
  }
  handler.ackOutput();
});
terminal.onData((text: string) => handler.write(text));
terminal.onResize(({cols, rows}) => handler.resize(cols, rows));

// A program that asks for the mouse, such as herdr or opencode, gets the wheel
// as mouse buttons at the cell under the pointer, one for each line scrolled.
// ghostty-web would send arrow keys on the alternate screen.
let wheelPixels = 0;
terminal.attachCustomWheelEventHandler((event: WheelEvent) => {
  const renderer = terminal.renderer;
  if (!renderer || !terminal.hasMouseTracking()) {
    return false;
  }
  const height = renderer.charHeight;
  const pixelsPerUnit = [1, height, height * terminal.rows];
  const pixels = event.deltaY * (pixelsPerUnit[event.deltaMode] ?? 1);
  if (Math.sign(pixels) !== Math.sign(wheelPixels)) {
    wheelPixels = 0;
  }
  wheelPixels += pixels;
  const lines = Math.trunc(wheelPixels / height);
  wheelPixels -= lines * height;
  if (!lines) {
    return true;
  }
  const canvas = renderer.getCanvas().getBoundingClientRect();
  const cell = (offset: number, size: number, count: number) =>
      Math.min(count, Math.max(1, Math.floor(offset / size) + 1));
  const col =
      cell(event.clientX - canvas.left, renderer.charWidth, terminal.cols);
  const row = cell(event.clientY - canvas.top, height, terminal.rows);
  const button = lines < 0 ? 64 : 65;
  const report = terminal.getMode(1006) ?
      `\x1b[<${button};${col};${row}M` :
      '\x1b[M' + String.fromCharCode(32 + button, 32 + col, 32 + row);
  handler.write(report.repeat(Math.abs(lines)));
  return true;
});

let layout: KeyboardLayoutMap|undefined;
function readLayout() {
  navigator.keyboard?.getLayoutMap().then(map => layout = map, () => {});
}

window.addEventListener('keydown', (event: KeyboardEvent) => {
  // Cmd shortcuts, Ctrl+Tab, and Ctrl+` are the browser's. ghostty-web would
  // type them into the shell, so it never sees them. They are not cancelled,
  // which leaves them for the browser.
  if (event.metaKey ||
      (event.ctrlKey && (event.code === 'Tab' || event.code === 'Backquote'))) {
    event.stopPropagation();
    return;
  }
  // Shift+Enter sends what Option+Enter does, which Claude Code reads as a new
  // line. ghostty-web would send Enter.
  if (event.key === 'Enter' && event.shiftKey && !event.ctrlKey &&
      !event.altKey && !event.isComposing) {
    event.stopPropagation();
    event.preventDefault();
    handler.write('\x1b\r');
    return;
  }
  // Option is Alt: the shell gets Esc and the key's own character, not the
  // symbol macOS makes of the two.
  const key = event.altKey && !event.ctrlKey ? layout?.get(event.code) : '';
  if (key?.length === 1) {
    event.stopPropagation();
    event.preventDefault();
    handler.write('\x1b' + (event.shiftKey ? key.toUpperCase() : key));
  }
}, {capture: true});

// Cmd-C and the Edit menu copy what is selected in the terminal.
document.addEventListener('copy', (event: ClipboardEvent) => {
  if (terminal.hasSelection()) {
    event.clipboardData?.setData('text/plain', terminal.getSelection());
    event.preventDefault();
  }
});

// A paste of files or an image types their paths. The page cannot see paths,
// so the browser reads them from the clipboard. ghostty-web pastes the rest.
document.addEventListener('paste', (event: ClipboardEvent) => {
  const data = event.clipboardData;
  if (!data?.files.length) {
    return;
  }
  event.preventDefault();
  event.stopImmediatePropagation();
  const text = data.getData('text/plain');
  handler.pastePaths().then(({paths}) => {
    if (paths || text) {
      terminal.paste(paths || text);
    }
  });
}, {capture: true});

// Dropped text is pasted, and the browser turns dropped files into their
// paths. Anything else dropped would replace the page.
document.addEventListener('dragover', event => event.preventDefault());
document.addEventListener('drop', (event: DragEvent) => {
  event.preventDefault();
  const text = event.dataTransfer?.getData('text/plain');
  if (text) {
    terminal.paste(text);
  }
});

window.addEventListener('focus', () => {
  terminal.focus();
  readLayout();
});
terminal.focus();
readLayout();
handler.start(terminal.cols, terminal.rows);
