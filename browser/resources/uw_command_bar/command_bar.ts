// Copyright 2026 The uw Authors. All rights reserved.

import {KeywordModeManager} from '//resources/cr_components/searchbox/keyword_mode_manager.js';
import {isMac} from '//resources/js/platform.js';
import {SuggestInventory} from '//resources/mojo/components/omnibox/browser/fusebox_action.mojom-webui.js';
import type {AutocompleteMatch, AutocompleteResult} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';
import {InputMethod, PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from '//resources/mojo/components/omnibox/browser/searchbox.mojom-webui.js';

import {Calculator} from './calculator.js';
import type {Calculation, Ink, MathJsLibrary} from './calculator.js';
import {TimeMath} from './time.js';
import {CalculatorHandler} from './uw_command_bar.mojom-webui.js';
import type {BarCommand, BarTask, SelectedPage} from './uw_command_bar.mojom-webui.js';

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

// Arc for Windows' keys: Ctrl for Cmd. Terminal tabs run a shell on the Mac
// only so far, so elsewhere the bar offers no terminal. See
// docs/plans/windows.md.
const hasTerminal = isMac;
if (!isMac) {
  document.querySelector('#askKey')!.firstChild!.textContent = 'Ctrl ';
  document.querySelector('#terminalKey')!.remove();
}

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
// At most this many command and task rows show among Chromium's.
const MOST_OWN_ROWS = 4;
const MOVE_TO_NEW_TASK = 'Move to New Task';

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
// A row of Chromium's, with the line Chromium knows it by, or one of uw's.
type MatchRow = {kind: 'match', match: AutocompleteMatch, line: number};
type Row = MatchRow|{kind: 'command', command: BarCommand}|
    {kind: 'task', task: BarTask}|{kind: 'newTask'};
let rows: Row[] = [];
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
// After a `@`, the text is a question about this page.
let page: SelectedPage|null = null;
// After Option+Cmd+M, the rows are the tasks the selected tabs can move to.
let moving = false;
// The commands that can run and the tasks, as of the bar showing, and the
// selected tab's task, empty for the Scratchpad.
let commands: BarCommand[] = [];
let tasks: BarTask[] = [];
let selectedTask = '';
// The latest request for them, which Chromium's rows wait for.
let lists = Promise.resolve();

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
  chip.hidden = !shell && !page && !moving && !keyword;
  chip.classList.toggle('shell', shell);
  chip.classList.toggle('page', !!page);
  if (page) {
    chip.replaceChildren(favicon(page.url), text('', page.title));
  } else {
    chip.textContent = shell ? '>' :
        moving               ? 'Move to task' :
                               (keywords.inputKeywordModel?.displayText ?? '');
  }
  field.placeholder = shell ? 'Type a shell command' :
      page                  ? 'Ask about this page' :
      moving                ? 'Type a task\'s name' :
                              PLACEHOLDER;
  keys.classList.toggle('shell', shell);
  keys.classList.toggle('page', !!page);
  keys.classList.toggle('moving', moving);
}

function svgIcon(id: string): SVGSVGElement {
  const svg = document.createElementNS(SVG, 'svg');
  const use = document.createElementNS(SVG, 'use');
  use.setAttribute('href', `#${id}`);
  svg.append(use);
  return svg;
}

function icon(content: Element): HTMLElement {
  const box = document.createElement('span');
  box.className = 'icon';
  box.append(content);
  return box;
}

// A task's folder in its color, or the Scratchpad's tray.
function taskIcon(task: BarTask): SVGSVGElement {
  const svg = svgIcon(task.id ? 'folder' : 'scratchpad');
  svg.style.color = task.color;
  return svg;
}

function favicon(url: string): HTMLImageElement {
  const image = document.createElement('img');
  image.alt = '';
  image.src = 'chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=' +
      encodeURIComponent(url);
  return image;
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

// The task an open tab is in, found by the address it shows.
function place(url: string): HTMLElement {
  const span = text('place', '');
  const task = tasks.find(task => task.urls.includes(url));
  if (task?.id) {
    span.append(taskIcon(task), text('', task.name));
  } else if (task) {
    span.textContent = task.name;
  }
  return span;
}

function caps(keys: string[]): HTMLElement {
  const span = document.createElement('span');
  span.className = 'caps';
  span.append(...keys.map(key => {
    const cap = document.createElement('kbd');
    cap.textContent = key;
    return cap;
  }));
  return span;
}

function cells(row: Row): HTMLElement[] {
  switch (row.kind) {
    case 'match': {
      const [title, detail] = describe(row.match);
      return [
        icon(row.match.type === 'search' ? svgIcon('search') :
                                           favicon(row.match.destinationUrl)),
        text('title', title),
        text('detail', detail),
        ...(row.match.type === 'tab' ? [place(row.match.destinationUrl)] : []),
        text('kind', KINDS[row.match.type] ?? ''),
      ];
    }
    case 'command':
      return [
        icon(svgIcon('command')),
        text('title', row.command.name),
        text('detail', ''),
        caps(row.command.keys),
        text('kind', 'Command'),
      ];
    case 'task': {
      const count = row.task.tabCount;
      return [
        icon(taskIcon(row.task)),
        text('title', row.task.name),
        text('detail', `${count} ${count === 1 ? 'tab' : 'tabs'}`),
        text('kind', moving ? '' : 'Task'),
      ];
    }
    case 'newTask':
      return [
        icon(svgIcon('folder-plus')),
        text('title', MOVE_TO_NEW_TASK),
        text('detail', ''),
      ];
  }
}

function nameOf(row: Exclude<Row, MatchRow>): string {
  switch (row.kind) {
    case 'command':
      return row.command.name;
    case 'task':
      return row.task.name;
    case 'newTask':
      return MOVE_TO_NEW_TASK;
  }
}

function words(value: string): string[] {
  return value.toLowerCase().split(/[^\p{L}\p{N}]+/u).filter(Boolean);
}

// 2 when the typed text starts `name`, 1 when each typed word starts one of
// its words, and 0 when it does not match.
function nameMatch(name: string): number {
  const typedWords = words(typed);
  const own = words(name);
  if (!typedWords.length ||
      !typedWords.every(word => own.some(part => part.startsWith(word)))) {
    return 0;
  }
  return name.toLowerCase().startsWith(typed.trimStart().toLowerCase()) ? 2 :
                                                                          1;
}

// Commands and tasks among Chromium's rows: an address being completed, a
// command or task whose name the typed text starts, the open tabs, the other
// commands and tasks, then history, bookmarks, and searches.
function withOwnRows(matches: MatchRow[]): Row[] {
  const own = [
    ...tasks.filter(task => task.id)
        .map(task => ({row: {kind: 'task', task} as Row, how: nameMatch(task.name)})),
    ...commands.map(
        command =>
            ({row: {kind: 'command', command} as Row, how: nameMatch(command.name)})),
  ].filter(found => found.how)
                  .sort((a, b) => b.how - a.how)
                  .slice(0, MOST_OWN_ROWS);
  const first = matches[0]?.match;
  const completing =
      !!first?.allowedToBeDefaultMatch && !!first.inlineAutocompletion;
  const rest = completing ? matches.slice(1) : matches;
  return [
    ...matches.slice(0, completing ? 1 : 0),
    ...own.filter(found => found.how === 2).map(found => found.row),
    ...rest.filter(row => row.match.type === 'tab'),
    ...own.filter(found => found.how === 1).map(found => found.row),
    ...rest.filter(row => row.match.type !== 'tab'),
  ];
}

function drawRows() {
  results.replaceChildren(...rows.map((row, index) => {
    const item = document.createElement('li');
    item.setAttribute('role', 'option');
    item.append(...cells(row));
    item.addEventListener('mousemove', () => {
      if (selected !== index) {
        select(index);
      }
    });
    item.addEventListener('click', () => open(index, false));
    return item;
  }));
}

// After Option+Cmd+M: Move to New Task, the other tasks, then the Scratchpad,
// those whose names match the typed text.
function showMoveRows() {
  const all: Row[] = [
    {kind: 'newTask'},
    ...tasks.filter(task => task.id !== selectedTask)
        .map(task => ({kind: 'task', task}) as Row),
  ];
  rows = typed.trim() ?
      all.filter(row => row.kind !== 'match' && nameMatch(nameOf(row))) :
      all;
  shownFor = typed;
  drawRows();
  select(0);
}

function actionName(row: Row|undefined): string {
  switch (row?.kind) {
    case 'command':
      return 'Run';
    case 'task':
      return moving ? 'Move here' : 'Go to task';
    case 'newTask':
      return 'Move here';
    case 'match':
      return row.match.type === 'tab' ? 'Switch to tab' : 'Open';
    default:
      return 'Open';
  }
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
                                      actionName(rows[index]);
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
  if (shell || page || moving || !typed) {
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
  if (row && row.kind !== 'match') {
    setField(nameOf(row));
    completion = '';
    paint();
    return;
  }
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

async function show(result: AutocompleteResult) {
  await lists;
  if (shell || page || moving || result.input !== typed) {
    return;
  }
  shownFor = typed;
  rows = withOwnRows(
      result.matches.map((match, line) => ({kind: 'match' as const, match, line}))
          .filter(row => !row.match.isHidden));
  drawRows();
  select(answered() ? -1 : 0);
  const top = rows[0];
  const first = top?.kind === 'match' ? top.match : null;
  keywords.onSelectedMatchChanged(first);

  // Complete the typed text with the first row, as the address bar does,
  // while the caret is at the end of what was typed. A sum is not completed.
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
  if (row?.kind === 'command') {
    calculatorHandler.runCommand(row.command.id);
    return;
  }
  if (row?.kind === 'task') {
    if (moving) {
      calculatorHandler.moveToTask(row.task.id);
    } else {
      calculatorHandler.goToTask(row.task.id);
    }
    return;
  }
  if (row?.kind === 'newTask') {
    calculatorHandler.moveToNewTask();
    return;
  }
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
  if (shell || page || moving) {
    return;
  }
  if (!typed && !keywords.isInKeywordMode) {
    showRows(false);
    handler.stopAutocomplete(/*clearResult=*/ true);
    return;
  }
  showRows(true);
  // What can run changes with the selected tab, so each query asks again.
  refreshLists();
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

// Asks uw about the selected page in a new chat.
function askAboutPage() {
  const words = field.value.trim();
  if (words) {
    calculatorHandler.askAboutPage(words);
  }
}

// The `>` and `@` chips take the text away from the rows and the calculator.
function leaveRows() {
  calculate();
  keywords.exit();
  showRows(false);
  handler.stopAutocomplete(/*clearResult=*/ true);
}

function setShell(on: boolean) {
  shell = on;
  if (on) {
    leaveRows();
  }
  showChip();
}

function setPage(selected: SelectedPage|null) {
  page = selected;
  if (page) {
    leaveRows();
  }
  showChip();
}

// A `@` in an empty field asks about the selected page, when it is a web page.
// Off one, it stays typed text.
async function startPageQuestion() {
  const {page: selected} = await calculatorHandler.getSelectedPage();
  if (!selected || shell || page || !field.value.startsWith('@')) {
    query(/*preventInlineAutocomplete=*/ false);
    return;
  }
  setField(field.value.slice(1));
  typed = field.value;
  completion = '';
  paint();
  setPage(selected);
}

// Kept text with a `@` chip follows the tab selected now.
async function refreshPage() {
  if (!page) {
    return;
  }
  const {page: selected} = await calculatorHandler.getSelectedPage();
  setPage(selected);
  if (!selected) {
    query(/*preventInlineAutocomplete=*/ true);
  }
}

function refreshLists() {
  lists = fetchLists();
}

// The commands and tasks for the rows, and whether the bar opened to move
// the selected tabs.
async function fetchLists() {
  const [{commands: listed}, {tasks: list}] = await Promise.all(
      [calculatorHandler.getCommands(), calculatorHandler.getTasks()]);
  commands = listed;
  tasks = list.tasks;
  selectedTask = list.selected;
  if (list.moving) {
    moving = true;
    leaveRows();
    showChip();
    showMoveRows();
  } else if (moving) {
    clear();
  }
}

function clear() {
  moving = false;
  field.value = '';
  typed = '';
  completion = '';
  paint();
  pendingReturn = false;
  calculate();
  keywords.exit();
  setShell(false);
  setPage(null);
  showRows(false);
  handler.stopAutocomplete(/*clearResult=*/ true);
}

router.autocompleteResultChanged.addListener(show);

field.addEventListener('input', event => {
  if (moving) {
    typed = field.value;
    paint();
    showMoveRows();
    return;
  }
  if (hasTerminal && !shell && field.value === '>' &&
      !keywords.isInKeywordMode) {
    field.value = '';
    typed = '';
    paint();
    setShell(true);
    return;
  }
  if (!shell && !page && field.value === '@' && !keywords.isInKeywordMode) {
    startPageQuestion();
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
      if (!moving) {
        fill();
      }
    }
  } else if (
      (event.key === 'ArrowRight' || event.key === 'End') &&
      !event.shiftKey && completion) {
    event.preventDefault();
    accept();
  } else if (event.key === 'Tab' && !event.shiftKey) {
    // Tab turns a completion into typed text, or starts a site search.
    event.preventDefault();
    const row = rows[selected];
    if (completion) {
      accept();
    } else {
      keywords.acceptTab(row?.kind === 'match' ? row.match : null, selected);
    }
  } else if (event.key === 'Backspace') {
    if (shell && !field.value) {
      event.preventDefault();
      setShell(false);
    } else if (page && !field.value) {
      event.preventDefault();
      setPage(null);
    } else if (keywords.handleBackspace(field)) {
      event.preventDefault();
    }
  } else if (event.key === 'Enter') {
    event.preventDefault();
    if (moving) {
      open(selected, true);
    } else if (page) {
      askAboutPage();
    } else if (isMac ? event.metaKey : event.ctrlKey) {
      sendText({metaKey: true});
    } else if (hasTerminal && (event.altKey || shell)) {
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

// The browser empties the field to forget what was typed, or sets it to ">",
// as if typed, when Cmd+Shift+P opens the command bar for a shell command.
router.setInputText.addListener((input: string) => {
  clear();
  setShell(input === '>');
  refreshLists();
});

// The bar is frosted glass while its window's glass is on.
async function refreshFrost() {
  const {frosted} = await calculatorHandler.isFrosted();
  document.body.classList.toggle('frosted', frosted);
}

// Escape empties the command bar, and closes it once the field is empty, or at
// once while it lists tasks to move to. The browser has the page forget its
// text whenever the panel closes for anything but a click elsewhere.
document.addEventListener('keydown', event => {
  if (event.key !== 'Escape') {
    return;
  }
  event.preventDefault();
  if (field.value && !moving) {
    clear();
  } else {
    calculatorHandler.close();
  }
});

// The browser holds keys until the field takes focus, so the field gives it up
// whenever the panel goes and takes it again each time the panel shows. It
// waits one task: during the window's focus event the browser has not yet heard
// which frame has focus, and ignores the field's.
window.addEventListener('focus', () => {
  setTimeout(() => field.focus());
  refreshFrost();
  refreshCurrencies();
  refreshClock();
  refreshPage();
  refreshLists();
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
refreshLists();
if (document.hasFocus()) {
  field.focus();
}
