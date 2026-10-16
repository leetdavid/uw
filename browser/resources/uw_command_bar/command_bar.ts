// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {KeywordModeManager} from '//resources/cr_components/searchbox/keyword_mode_manager.js';
import {SuggestInventory} from '//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js';
import type {AutocompleteMatch, AutocompleteResult} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';
import {InputMethod, PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';

const field = document.querySelector<HTMLInputElement>('#field')!;
const chip = document.querySelector<HTMLElement>('#chip')!;
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
const PLACEHOLDER = field.placeholder;

let queryId = 0;
// What the user typed. The field can also hold a completion after it.
let typed = '';
// Each shown row, and the line Chromium knows it by.
let rows: Array<{match: AutocompleteMatch, line: number}> = [];
// The typed text the shown rows are for.
let shownFor = '';
let selected = 0;
// A Return pressed before the rows for the latest text arrived.
let pendingReturn = false;
// After a `>` or Cmd+Shift+P, the text is a shell command.
let shell = false;

const keywords = new KeywordModeManager({
  onKeywordModelChanged: showChip,
  onKeywordCleared: event => {
    field.value = event.restoredText;
    field.setSelectionRange(event.cursorPosition, event.cursorPosition);
    query(/*preventInlineAutocomplete=*/ true);
  },
  onKeywordEntered: () => {
    field.value = '';
    query(/*preventInlineAutocomplete=*/ false);
  },
});

function showChip() {
  const keyword = keywords.isInKeywordMode;
  chip.hidden = !shell && !keyword;
  chip.classList.toggle('shell', shell);
  chip.textContent =
      shell ? '>' : (keywords.inputKeywordModel?.displayText ?? '');
  field.placeholder = shell ? 'Type a shell command' : PLACEHOLDER;
  keys.classList.toggle('shell', shell);
}

function svgIcon(id: string): SVGSVGElement {
  const svg = document.createElementNS(SVG, 'svg');
  const use = document.createElementNS(SVG, 'use');
  use.setAttribute('href', `#${id}`);
  svg.append(use);
  return svg;
}

function icon(match: AutocompleteMatch): HTMLElement {
  const box = document.createElement('span');
  box.className = 'icon';
  if (match.type === 'search') {
    box.append(svgIcon('search'));
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

// A page reads as its title and site, a search as its words. A row that can
// start a site search says so.
function describe(match: AutocompleteMatch): [string, string] {
  if (match.keywordModel && !keywords.isInKeywordMode) {
    return [match.contents, `Tab to ${match.keywordModel.chipHint}`];
  }
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

// The field's text without a completion still selected after the caret.
function typedText(): string {
  const completing = field.selectionStart !== field.selectionEnd &&
      field.selectionEnd === field.value.length;
  return completing ? field.value.slice(0, field.selectionStart ?? 0) :
                      field.value;
}

function show(result: AutocompleteResult) {
  if (shell || result.input !== typed) {
    return;
  }
  shownFor = typed;
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
  keywords.onSelectedMatchChanged(rows[0]?.match ?? null);

  // Complete the typed text with the first row, as the address bar does,
  // while the caret is at the end of what was typed.
  const first = rows[0]?.match;
  if (first?.allowedToBeDefaultMatch && first.inlineAutocompletion &&
      field.value === typed && field.selectionStart === typed.length) {
    field.value = typed + first.inlineAutocompletion;
    field.setSelectionRange(typed.length, field.value.length);
  }

  if (pendingReturn) {
    pendingReturn = false;
    open(0, true);
  }
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

function showRows(on: boolean) {
  if (!on) {
    rows = [];
    shownFor = '';
    results.replaceChildren();
  }
  keys.classList.toggle('typing', on || shell);
}

function query(preventInlineAutocomplete: boolean) {
  typed = field.value;
  if (shell) {
    return;
  }
  if (!typed && !keywords.isInKeywordMode) {
    showRows(false);
    handler.stopAutocomplete(/*clearResult=*/ true);
    return;
  }
  showRows(true);
  handler.queryAutocomplete(
      ++queryId, typed, preventInlineAutocomplete,
      field.selectionStart ?? typed.length, SuggestInventory.kDefault,
      /*isOnFocus=*/ false, keywords.activeKeyword, InputMethod.kKeyboard);
}

// Hands the typed text to uw: Cmd asks it in a new chat, Option runs it in a
// new terminal. The browser takes the text from the query.
function sendText(key: {metaKey?: boolean, altKey?: boolean}) {
  const words = typedText().trim();
  if (!words) {
    return;
  }
  handler.queryAutocomplete(
      ++queryId, words, /*preventInlineAutocomplete=*/ true, words.length,
      SuggestInventory.kDefault, /*isOnFocus=*/ false, /*keyword=*/ '',
      InputMethod.kKeyboard);
  handler.openAutocompleteMatch(
      NO_ROW, '', /*areMatchesShowing=*/ true, /*mouseButton=*/ 0, {
        altKey: key.altKey ?? false,
        ctrlKey: false,
        metaKey: key.metaKey ?? false,
        shiftKey: false,
      },
      /*viaKeyboard=*/ true);
}

function setShell(on: boolean) {
  shell = on;
  if (on) {
    keywords.exit();
    showRows(false);
    handler.stopAutocomplete(/*clearResult=*/ true);
  }
  showChip();
}

function clear() {
  field.value = '';
  typed = '';
  pendingReturn = false;
  keywords.exit();
  setShell(false);
  showRows(false);
  handler.stopAutocomplete(/*clearResult=*/ true);
}

router.autocompleteResultChanged.addListener(show);

field.addEventListener('input', event => {
  if (!shell && field.value === '>' && !keywords.isInKeywordMode) {
    field.value = '';
    typed = '';
    setShell(true);
    return;
  }
  if (!shell &&
      keywords.acceptInputTrigger(field.value, field.selectionStart)) {
    return;
  }
  // As the address bar does, nothing is completed while deleting or with the
  // caret before the end.
  const deleting =
      event instanceof InputEvent && event.inputType.startsWith('delete');
  query(deleting || field.selectionStart !== field.value.length);
});

field.addEventListener('keydown', event => {
  if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
    event.preventDefault();
    if (rows.length) {
      const step = event.key === 'ArrowDown' ? 1 : -1;
      select((selected + step + rows.length) % rows.length);
    }
  } else if (event.key === 'Tab' && !event.shiftKey) {
    // Tab turns a completion into typed text, or starts a site search.
    event.preventDefault();
    if (typedText() !== field.value) {
      field.setSelectionRange(field.value.length, field.value.length);
      query(/*preventInlineAutocomplete=*/ true);
    } else {
      keywords.acceptTab(rows[selected]?.match ?? null, selected);
    }
  } else if (event.key === 'Backspace') {
    if (shell && !field.value) {
      event.preventDefault();
      setShell(false);
    } else if (keywords.handleBackspace(field)) {
      event.preventDefault();
    }
  } else if (event.key === 'Enter') {
    event.preventDefault();
    if (event.metaKey) {
      sendText({metaKey: true});
    } else if (event.altKey || shell) {
      sendText({altKey: true});
    } else if (!event.ctrlKey) {
      if (shownFor === typed) {
        open(selected, true);
      } else {
        pendingReturn = true;
      }
    }
  } else if (event.key === 'k' && event.metaKey) {
    // Cmd+K comes with T63.
    event.preventDefault();
  }
});

// Cmd+Shift+P opens the command bar ready for a shell command: the browser
// sets the field to ">", as if typed.
router.setInputText.addListener((input: string) => {
  clear();
  setShell(input === '>');
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
