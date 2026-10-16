// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {SuggestInventory} from '//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js';
import type {AutocompleteMatch, AutocompleteResult} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';
import {InputMethod, PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';

const field = document.querySelector<HTMLInputElement>('#field')!;
const results = document.querySelector<HTMLUListElement>('#results')!;
const keys = document.querySelector<HTMLElement>('#keys')!;
const action = document.querySelector<HTMLElement>('#action')!;

const router = new PageCallbackRouter();
const handler = new PageHandlerRemote();
PageHandlerFactory.getRemote().createPageHandler(
    router.$.bindNewPipeAndPassRemote(),
    handler.$.bindNewPipeAndPassReceiver());

// The browser names each row's kind in its type.
const KINDS: Record<string, string> = {
  tab: 'Tab',
  bookmark: 'Bookmark',
  history: 'History',
  search: 'Search',
};
const SVG = 'http://www.w3.org/2000/svg';
// The line the searchbox reads as the typed text with no row picked.
const NO_ROW = 255;

let queryId = 0;
// Each shown row, and the line Chromium knows it by.
let rows: Array<{match: AutocompleteMatch, line: number}> = [];
let selected = 0;

function icon(match: AutocompleteMatch): HTMLElement {
  const box = document.createElement('span');
  box.className = 'icon';
  if (match.type === 'search') {
    const svg = document.createElementNS(SVG, 'svg');
    const use = document.createElementNS(SVG, 'use');
    use.setAttribute('href', '#search');
    svg.append(use);
    box.append(svg);
    return box;
  }
  const image = document.createElement('img');
  image.alt = '';
  image.src = 'chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=' +
      encodeURIComponent(match.destinationUrl);
  box.append(image);
  return box;
}

function text(className: string, value: string): HTMLElement {
  const span = document.createElement('span');
  span.className = className;
  span.textContent = value;
  return span;
}

// A page reads as its title and site, a search as its words.
function describe(match: AutocompleteMatch): [string, string] {
  if (match.type === 'search' || !match.description) {
    return [match.contents, ''];
  }
  const url = match.destinationUrl;
  const site =
      URL.canParse(url) ? new URL(url).host.replace(/^www\./, '') : '';
  return [match.description, site];
}

function select(index: number) {
  selected = index;
  results.querySelectorAll('li').forEach((row, i) => {
    row.classList.toggle('selected', i === index);
    row.setAttribute('aria-selected', String(i === index));
  });
  action.textContent =
      rows[index]?.match.type === 'tab' ? 'Switch to tab' : 'Open';
}

function show(result: AutocompleteResult) {
  if (result.input !== field.value) {
    return;
  }
  rows = result.matches.map((match, line) => ({match, line}))
             .filter(row => !row.match.isHidden);
  results.replaceChildren(...rows.map(({match}, index) => {
    const row = document.createElement('li');
    row.setAttribute('role', 'option');
    const [title, detail] = describe(match);
    row.append(
        icon(match), text('title', title), text('detail', detail),
        text('kind', KINDS[match.type] ?? ''));
    row.addEventListener('mousemove', () => {
      if (selected !== index) {
        select(index);
      }
    });
    row.addEventListener('click', () => open(index, false));
    return row;
  }));
  select(0);
}

function open(index: number, viaKeyboard: boolean) {
  const row = rows[index];
  if (!row && !field.value) {
    return;
  }
  handler.openAutocompleteMatch(
      row ? row.line : NO_ROW, row ? row.match.destinationUrl : '',
      /*areMatchesShowing=*/ true, /*mouseButton=*/ 0,
      {altKey: false, ctrlKey: false, metaKey: false, shiftKey: false},
      viaKeyboard);
}

function clear() {
  field.value = '';
  rows = [];
  results.replaceChildren();
  keys.classList.remove('typing');
  handler.stopAutocomplete(/*clearResult=*/ true);
}

router.autocompleteResultChanged.addListener(show);

field.addEventListener('input', () => {
  if (!field.value) {
    clear();
    return;
  }
  keys.classList.add('typing');
  handler.queryAutocomplete(
      ++queryId, field.value, /*preventInlineAutocomplete=*/ true,
      field.selectionStart ?? field.value.length, SuggestInventory.kDefault,
      /*isOnFocus=*/ false, /*keyword=*/ '', InputMethod.kKeyboard);
});

field.addEventListener('keydown', event => {
  if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
    event.preventDefault();
    if (rows.length) {
      const step = event.key === 'ArrowDown' ? 1 : -1;
      select((selected + step + rows.length) % rows.length);
    }
  } else if (event.key === 'Enter') {
    event.preventDefault();
    // Cmd+Return and Option+Return come with T81 and T82.
    if (!event.metaKey && !event.altKey && !event.ctrlKey) {
      open(selected, true);
    }
  } else if (event.key === 'k' && event.metaKey) {
    // Cmd+K comes with T63.
    event.preventDefault();
  }
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clear();
  }
});

// The browser holds keys until the field takes focus, so the field gives it up
// whenever the panel goes and takes it again each time the panel shows. It
// waits one task: during the window's focus event the browser has not yet heard
// which frame has focus, and ignores the field's.
window.addEventListener('focus', () => setTimeout(() => field.focus()));
window.addEventListener('blur', () => field.blur());
if (document.hasFocus()) {
  field.focus();
}
