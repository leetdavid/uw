// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

import {KeywordModeManager} from '//resources/cr_components/searchbox/keyword_mode_manager.js';
import {SuggestInventory} from '//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js';
import type {AutocompleteMatch, AutocompleteResult} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';
import {InputMethod, PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';

import {Calculator} from './calculator.js';
import type {Calculation, Ink, MathJsLibrary} from './calculator.js';
import {TimeMath} from './time.js';
import {CalculatorHandler} from './uw_command_bar.mojom-webui.js';

// mathjs, which command_bar.html loads first.
declare const math: MathJsLibrary;

const field = document.querySelector<HTMLInputElement>('#field')!;
const ink = document.querySelector<HTMLElement>('#ink')!;
const chip = document.querySelector<HTMLElement>('#chip')!;
const results = document.querySelector<HTMLUListElement>('#results')!;
const keys = document.querySelector<HTMLElement>('#keys')!;
const action = document.querySelector<HTMLElement>('#action')!;
const card = document.querySelector<HTMLElement>('#calc')!;
const sum = document.querySelector<HTMLElement>('#sum')!;
const sumName = document.querySelector<HTMLElement>('#sumName')!;
const answer = document.querySelector<HTMLElement>('#answer')!;
const answerName = document.querySelector<HTMLElement>('#answerName')!;
const arrowIcon = document.querySelector<SVGUseElement>('#arrowIcon')!;
const note = document.querySelector<HTMLElement>('#calcNote')!;

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

const calculator = new Calculator(math);
const timeMath = new TimeMath(calculator);
const calculatorHandler = CalculatorHandler.getRemote();
// Works a card built on the clock out again when the minute changes.
let ticker = 0;

let queryId = 0;
// What the user typed. The arrow keys can put a row's text in the field.
let typed = '';
// The rest of the address being typed, drawn after the caret but not in the
// field, so the caret stays where the typing goes.
let completion = '';
// Each shown row, and the line Chromium knows it by.
let rows: Array<{match: AutocompleteMatch, line: number}> = [];
// The typed text the shown rows are for.
let shownFor = '';
// The selected row, or -1 for the calculator's answer.
let selected = 0;
// The calculator's card for what is typed, if it reads as math.
let calculation: Calculation|null = null;
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

function answered(): boolean {
  return !!calculation && !calculation.why;
}

function select(index: number) {
  selected = index;
  card.classList.toggle('selected', index === -1);
  card.setAttribute('aria-selected', String(index === -1));
  results.querySelectorAll('li').forEach((row, i) => {
    row.classList.toggle('selected', i === index);
    row.setAttribute('aria-selected', String(i === index));
  });
  action.textContent = index === -1 ? `Copy ${calculation?.copy ?? ''}` :
      rows[index]?.match.type === 'tab' ? 'Switch to tab' :
                                          'Open';
}

// Draws numbers in full ink, units lighter, operators lightest, and what was
// set aside underlined. A piece's leading space stays outside its span.
function draw(element: HTMLElement, ink: Ink[]) {
  element.replaceChildren(...ink.flatMap(part => {
    const span = document.createElement('span');
    span.textContent = part.text.trimStart();
    if (part.kind !== 'number') {
      span.className = `ink-${part.kind}`;
    }
    return span.textContent === part.text ? [span] : [' ', span];
  }));
}

// Time math first, and math for text with nothing about time in it.
function workOut(): Calculation|null {
  if (shell || !typed) {
    return null;
  }
  const timed = timeMath.calculate(typed);
  return timed === undefined ? calculator.calculate(typed) : timed;
}

function drawCard() {
  card.hidden = !calculation;
  note.hidden = !calculation?.note;
  if (calculation) {
    card.classList.toggle('no', !!calculation.why);
    draw(sum, calculation.sum);
    sumName.textContent = calculation.sumName;
    if (calculation.why) {
      answer.textContent = calculation.why;
      answerName.textContent = 'No answer';
    } else {
      draw(answer, calculation.answer);
      answerName.textContent = calculation.answerName;
    }
    answer.className = calculation.why ? '' : calculation.size ?? '';
    arrowIcon.setAttribute(
        'href', calculation.why ? '#no-answer' : '#arrow-right');
    note.textContent = calculation.note;
  }
}

// A card built on the clock is worked out again when the minute changes, and
// the selected row stays selected.
function keepTime() {
  clearTimeout(ticker);
  if (!calculation?.live) {
    return;
  }
  ticker = setTimeout(() => {
    calculation = workOut();
    drawCard();
    select(selected === -1 && !answered() ? 0 : selected);
    keepTime();
  }, 60_000 - Date.now() % 60_000);
}

// Works out the typed text at once, before Chromium's rows arrive, and
// selects the answer when there is one.
function calculate() {
  calculation = workOut();
  drawCard();
  keepTime();
  select(answered() ? -1 : 0);
}

function copyAnswer() {
  if (calculation && !calculation.why) {
    calculatorHandler.copyAnswer(calculation.copy);
  }
}

async function refreshCurrencies() {
  const {rates, currency} = await calculatorHandler.getCurrencies();
  calculator.setCurrencies(
      rates ? {date: rates.date, perEuro: rates.perEuro} : null, currency);
}

async function refreshClock() {
  const {timeZone, twentyFourHour} = await calculatorHandler.getClock();
  timeMath.setClock(timeZone, twentyFourHour);
}

// The field's own text is invisible: #ink draws it. What was typed is in full
// ink, and a completion after it, or the rest of a row's text, is muted.
function paint() {
  const value = field.value;
  const own =
      value.slice(0, typed.length).toLowerCase() === typed.toLowerCase() ?
      typed.length :
      0;
  ink.replaceChildren(
      value.slice(0, own), text('filled', value.slice(own) + completion));
  ink.scrollLeft = field.scrollLeft;
}

// The arrow keys put the selected row's text in the field, as the address bar
// does: a page's address or a search's words, with the caret at the end to
// edit it. The first row and the calculator's card bring back what was typed.
function fill() {
  const row = rows[selected];
  const first = row?.line === 0 && row.match.allowedToBeDefaultMatch;
  setField(
      row && !first ?
          keywords.formatMatchFillIntoEdit(row.match, row.line, typed) :
          typed);
  completion = row && first ? row.match.inlineAutocompletion : '';
  paint();
}

// Tab, → and End turn the completion into typed text.
function accept() {
  setField(typed + completion);
  query(/*preventInlineAutocomplete=*/ true);
}

// Sets the field's text with the caret at its end, scrolled into view, which
// the field does by itself only for typing.
function setField(value: string) {
  field.value = value;
  field.setSelectionRange(value.length, value.length);
  field.scrollLeft = field.scrollWidth;
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
  select(answered() ? -1 : 0);
  keywords.onSelectedMatchChanged(rows[0]?.match ?? null);

  // Complete the typed text with the first row, as the address bar does,
  // while the caret is at the end of what was typed. A sum is not completed.
  const first = rows[0]?.match;
  completion = !answered() && first?.allowedToBeDefaultMatch &&
          field.value === typed && field.selectionStart === typed.length ?
      first.inlineAutocompletion :
      '';
  paint();

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
  const completed = typed + completion;
  const grew = field.value.length > typed.length;
  typed = field.value;
  calculate();
  // Typing the completion's next letters keeps the rest of it until the rows
  // for the new text arrive.
  completion = !preventInlineAutocomplete && grew && !answered() &&
          completed.slice(0, typed.length).toLowerCase() ===
              typed.toLowerCase() ?
      completed.slice(typed.length) :
      '';
  paint();
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
  const words = field.value.trim();
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
    calculate();
    keywords.exit();
    showRows(false);
    handler.stopAutocomplete(/*clearResult=*/ true);
  }
  showChip();
}

function clear() {
  field.value = '';
  typed = '';
  completion = '';
  paint();
  pendingReturn = false;
  calculate();
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
    paint();
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
    const top = answered() ? -1 : 0;
    const count = rows.length - top;
    if (count > 0) {
      const step = event.key === 'ArrowDown' ? 1 : -1;
      select((selected - top + step + count) % count + top);
      fill();
    }
  } else if (
      (event.key === 'ArrowRight' || event.key === 'End') &&
      !event.shiftKey && completion) {
    event.preventDefault();
    accept();
  } else if (event.key === 'Tab' && !event.shiftKey) {
    // Tab turns a completion into typed text, or starts a site search.
    event.preventDefault();
    if (completion) {
      accept();
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
    } else if (selected === -1) {
      copyAnswer();
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
window.addEventListener('focus', () => {
  setTimeout(() => field.focus());
  refreshCurrencies();
  refreshClock();
});
window.addEventListener('blur', () => field.blur());
// Moving the caret off the end of what was typed drops the completion.
field.addEventListener('selectionchange', () => {
  if (completion &&
      (field.selectionStart !== typed.length ||
       field.selectionEnd !== typed.length)) {
    query(/*preventInlineAutocomplete=*/ true);
  }
});
field.addEventListener('scroll', () => {
  ink.scrollLeft = field.scrollLeft;
});
card.addEventListener('mousemove', () => {
  if (answered() && selected !== -1) {
    select(-1);
  }
});
card.addEventListener('click', copyAnswer);

refreshCurrencies();
refreshClock();
if (document.hasFocus()) {
  field.focus();
}
