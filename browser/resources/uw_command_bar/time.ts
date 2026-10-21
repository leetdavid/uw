// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

/// <reference lib="esnext.temporal" />

// The command bar's time math: times, time zones, dates, the gaps between
// them, and Unix timestamps. uw reads the typed text, Temporal works it out,
// and Intl writes the answer. The Calculator converts lengths of time to other
// units. The rules are in docs/plans/command-bar.md, under "Time and time
// zones".

import type {Calculation, Calculator, Ink} from './calculator.js';

// Each zone abbreviation stands for one place and follows its clock.
const ABBREVIATIONS: Array<[string, string, string]> = [
  ['EST', 'America/New_York', 'New York'],
  ['EDT', 'America/New_York', 'New York'],
  ['ET', 'America/New_York', 'New York'],
  ['CST', 'America/Chicago', 'Chicago'],
  ['CDT', 'America/Chicago', 'Chicago'],
  ['CT', 'America/Chicago', 'Chicago'],
  ['MST', 'America/Denver', 'Denver'],
  ['MDT', 'America/Denver', 'Denver'],
  ['MT', 'America/Denver', 'Denver'],
  ['PST', 'America/Los_Angeles', 'Los Angeles'],
  ['PDT', 'America/Los_Angeles', 'Los Angeles'],
  ['PT', 'America/Los_Angeles', 'Los Angeles'],
  ['AKST', 'America/Anchorage', 'Anchorage'],
  ['AKDT', 'America/Anchorage', 'Anchorage'],
  ['HST', 'Pacific/Honolulu', 'Honolulu'],
  ['AST', 'America/Halifax', 'Halifax'],
  ['ADT', 'America/Halifax', 'Halifax'],
  ['NST', 'America/St_Johns', 'St. John’s'],
  ['NDT', 'America/St_Johns', 'St. John’s'],
  ['BST', 'Europe/London', 'London'],
  ['WET', 'Europe/Lisbon', 'Lisbon'],
  ['WEST', 'Europe/Lisbon', 'Lisbon'],
  ['CET', 'Europe/Paris', 'Paris'],
  ['CEST', 'Europe/Paris', 'Paris'],
  ['EET', 'Europe/Athens', 'Athens'],
  ['EEST', 'Europe/Athens', 'Athens'],
  ['MSK', 'Europe/Moscow', 'Moscow'],
  ['IST', 'Asia/Kolkata', 'India'],
  ['PKT', 'Asia/Karachi', 'Pakistan'],
  ['GST', 'Asia/Dubai', 'Dubai'],
  ['ICT', 'Asia/Bangkok', 'Bangkok'],
  ['WIB', 'Asia/Jakarta', 'Jakarta'],
  ['SGT', 'Asia/Singapore', 'Singapore'],
  ['HKT', 'Asia/Hong_Kong', 'Hong Kong'],
  ['PHT', 'Asia/Manila', 'Manila'],
  ['JST', 'Asia/Tokyo', 'Japan'],
  ['KST', 'Asia/Seoul', 'Korea'],
  ['AEST', 'Australia/Sydney', 'Sydney'],
  ['AEDT', 'Australia/Sydney', 'Sydney'],
  ['ACST', 'Australia/Adelaide', 'Adelaide'],
  ['ACDT', 'Australia/Adelaide', 'Adelaide'],
  ['AWST', 'Australia/Perth', 'Perth'],
  ['NZST', 'Pacific/Auckland', 'New Zealand'],
  ['NZDT', 'Pacific/Auckland', 'New Zealand'],
  ['SAST', 'Africa/Johannesburg', 'South Africa'],
  ['BRT', 'America/Sao_Paulo', 'São Paulo'],
];

// Big cities that share another city's zone, and a few places whose zone
// names miss them.
const CITIES: Array<[string, string]> = [
  ['San Francisco', 'America/Los_Angeles'],
  ['Seattle', 'America/Los_Angeles'],
  ['San Diego', 'America/Los_Angeles'],
  ['Las Vegas', 'America/Los_Angeles'],
  ['Portland', 'America/Los_Angeles'],
  ['Sacramento', 'America/Los_Angeles'],
  ['Boston', 'America/New_York'],
  ['Washington', 'America/New_York'],
  ['Philadelphia', 'America/New_York'],
  ['Miami', 'America/New_York'],
  ['Atlanta', 'America/New_York'],
  ['Pittsburgh', 'America/New_York'],
  ['Baltimore', 'America/New_York'],
  ['Charlotte', 'America/New_York'],
  ['Dallas', 'America/Chicago'],
  ['Houston', 'America/Chicago'],
  ['Austin', 'America/Chicago'],
  ['San Antonio', 'America/Chicago'],
  ['Minneapolis', 'America/Chicago'],
  ['Nashville', 'America/Chicago'],
  ['New Orleans', 'America/Chicago'],
  ['Kansas City', 'America/Chicago'],
  ['Salt Lake City', 'America/Denver'],
  ['Montreal', 'America/Toronto'],
  ['Ottawa', 'America/Toronto'],
  ['Rio de Janeiro', 'America/Sao_Paulo'],
  ['Beijing', 'Asia/Shanghai'],
  ['Shenzhen', 'Asia/Shanghai'],
  ['Guangzhou', 'Asia/Shanghai'],
  ['China', 'Asia/Shanghai'],
  ['Mumbai', 'Asia/Kolkata'],
  ['Delhi', 'Asia/Kolkata'],
  ['New Delhi', 'Asia/Kolkata'],
  ['Bangalore', 'Asia/Kolkata'],
  ['Bengaluru', 'Asia/Kolkata'],
  ['Chennai', 'Asia/Kolkata'],
  ['Hyderabad', 'Asia/Kolkata'],
  ['Kolkata', 'Asia/Kolkata'],
  ['Osaka', 'Asia/Tokyo'],
  ['Kyoto', 'Asia/Tokyo'],
  ['Busan', 'Asia/Seoul'],
  ['Hanoi', 'Asia/Ho_Chi_Minh'],
  ['Ho Chi Minh City', 'Asia/Ho_Chi_Minh'],
  ['Saigon', 'Asia/Ho_Chi_Minh'],
  ['Kathmandu', 'Asia/Kathmandu'],
  ['Kyiv', 'Europe/Kyiv'],
  ['Abu Dhabi', 'Asia/Dubai'],
  ['Tel Aviv', 'Asia/Jerusalem'],
  ['Canberra', 'Australia/Sydney'],
  ['Wellington', 'Pacific/Auckland'],
  ['Cape Town', 'Africa/Johannesburg'],
  ['Barcelona', 'Europe/Madrid'],
  ['Munich', 'Europe/Berlin'],
  ['Frankfurt', 'Europe/Berlin'],
  ['Hamburg', 'Europe/Berlin'],
  ['Milan', 'Europe/Rome'],
  ['Geneva', 'Europe/Zurich'],
  ['Manchester', 'Europe/London'],
  ['Edinburgh', 'Europe/London'],
  ['UK', 'Europe/London'],
  ['Saint Petersburg', 'Europe/Moscow'],
];

const MONTHS: Record<string, number> = {
  jan: 1, january: 1, feb: 2, february: 2, mar: 3, march: 3,
  apr: 4, april: 4, may: 5, jun: 6, june: 6, jul: 7, july: 7,
  aug: 8, august: 8, sep: 9, sept: 9, september: 9, oct: 10, october: 10,
  nov: 11, november: 11, dec: 12, december: 12,
};

// Monday is 1, as in Temporal.
const WEEKDAYS: Record<string, number> = {
  mon: 1, monday: 1, tue: 2, tues: 2, tuesday: 2, wed: 3, wednesday: 3,
  thu: 4, thur: 4, thurs: 4, thursday: 4, fri: 5, friday: 5,
  sat: 6, saturday: 6, sun: 7, sunday: 7,
};

const DAY_WORDS = new Set(['now', 'today', 'tomorrow', 'yesterday']);

type Field = 'years'|'months'|'weeks'|'days'|'hours'|'minutes'|'seconds'|
    'milliseconds';
type Fields = Partial<Record<Field, number>>;

const DURATION_UNITS: Record<string, Field> = {
  s: 'seconds', sec: 'seconds', secs: 'seconds', second: 'seconds',
  seconds: 'seconds', min: 'minutes', mins: 'minutes', minute: 'minutes',
  minutes: 'minutes', h: 'hours', hr: 'hours', hrs: 'hours', hour: 'hours',
  hours: 'hours', d: 'days', day: 'days', days: 'days', w: 'weeks',
  wk: 'weeks', wks: 'weeks', week: 'weeks', weeks: 'weeks', mo: 'months',
  month: 'months', months: 'months', y: 'years', yr: 'years', yrs: 'years',
  year: 'years', years: 'years',
};

// A part of a length of time that is not whole moves to the next smaller unit.
const SMALLER: Partial<Record<Field, [Field, number]>> = {
  weeks: ['days', 7],
  days: ['hours', 24],
  hours: ['minutes', 60],
  minutes: ['seconds', 60],
  seconds: ['milliseconds', 1000],
};

const DAY_MS = 86_400_000;

type Token = {type: 'number', text: string, value: number}|{
  type: 'clock',
  text: string,
  hour: number,
  minute: number,
  second: number,
  half: 'am'|'pm'|'',
  // As typed, for "13:75 is not a time".
  shown: string,
}|{type: 'iso', text: string, year: number, month: number, day: number}|
    {type: 'word', text: string, word: string}|{type: 'symbol', text: string};

interface Place {
  zone: string;
  // In a pill, as in "Today, London".
  name: string;
  // In the sum, as in "3 PM London".
  shown: string;
  // Under the sum, as in "London time".
  label: string;
}

interface DateParts {
  month: number;
  day: number;
  year?: number;
  // As typed, for "Feb 30 is not a date".
  text: string;
}

interface Moment {
  type: 'moment';
  day?: string;
  date?: DateParts;
  weekday?: {day: number, which: ''|'next'|'last'};
  clock?: Extract<Token, {type: 'clock'}>;
  place?: Place;
  ink: Ink[];
}

type Term = Moment|{type: 'duration', fields: Fields, ink: Ink[]}|
    {type: 'amount', kind: string, ink: Ink[]}|
    {type: 'number', text: string, value: number, ink: Ink[]};

type Target = {type: 'place', place: Place}|{type: 'unix'}|{type: 'date'}|
    {type: 'unit', word: string};

interface Reading {
  terms: Term[];
  ops: Array<'+'|'-'>;
  target: Target|null;
  aside: string;
}

type Value = {
  type: 'moment',
  at: Temporal.ZonedDateTime,
  zone: string,
  time: boolean,
}|{type: 'duration', fields: Fields}|
    {type: 'days', days: number, from?: Temporal.PlainDate,
     to?: Temporal.PlainDate}|
    {type: 'span', ms: number}|{type: 'amount', kind: string}|{type: 'number'};

// Text with a word that is not read: no card.
class Unread extends Error {}

// Text read in full that does not work out: a card that says why.
class NoAnswer extends Error {}

const PATTERNS: Array<[string, RegExp]> = [
  ['space', /\s+/y],
  ['iso', /(\d{4})-(\d{2})-(\d{2})(?![\d:])/y],
  ['half', /(\d{1,2})(?::(\d{2}))?(?::(\d{2}))?\s*([ap])\.?m\.?(?![\p{L}\d])/iuy],
  ['clock', /(\d{1,2}):(\d{2})(?::(\d{2}))?(?![\d:])/y],
  ['word', /(?:utc|gmt)[+-]\d{1,2}(?::?\d{2})?(?!\d)/iy],
  ['word', /\p{L}+(?:\/[\p{L}\d_+-]+)+/uy],
  ['number', /\d+(?:\.\d+)?|\.\d+/y],
  ['word', /\p{L}[\p{L}\p{M}'’]*/uy],
  ['symbol', /\S/y],
];

function simplify(text: string): string {
  return text.normalize('NFD')
      .replace(/\p{M}/gu, '')
      .toLowerCase()
      .replace(/[_\s]+/g, ' ')
      .trim();
}

function tokenize(text: string): Token[] {
  const tokens: Token[] = [];
  const source = text.replace(/−/g, '-');
  let at = 0;
  while (at < source.length) {
    for (const [type, pattern] of PATTERNS) {
      pattern.lastIndex = at;
      const match = pattern.exec(source);
      if (!match) {
        continue;
      }
      const [all, a = '', b = '', c = '', d = ''] = match;
      at += all.length;
      if (type === 'iso') {
        tokens.push({type, text: all, year: +a, month: +b, day: +c});
      } else if (type === 'half') {
        const half = d.toLowerCase() === 'p' ? 'pm' : 'am';
        tokens.push({
          type: 'clock',
          text: all,
          hour: +a,
          minute: +b,
          second: +c,
          half,
          shown: `${[a, b, c].filter(part => part).join(':')} ${
              half.toUpperCase()}`,
        });
      } else if (type === 'clock') {
        tokens.push({
          type,
          text: all,
          hour: +a,
          minute: +b,
          second: +c,
          half: '',
          shown: all,
        });
      } else if (type === 'number') {
        tokens.push({type, text: all, value: Number(all)});
      } else if (type === 'word') {
        tokens.push({type, text: all, word: simplify(all)});
      } else if (type === 'symbol') {
        tokens.push({type, text: all});
      }
      break;
    }
  }
  return tokens;
}

function wordOf(token: Token|undefined): string {
  return token?.type === 'word' ? token.word : '';
}

function isAboutTime(token: Token): boolean {
  const word = wordOf(token);
  return token.type === 'clock' || token.type === 'iso' || DAY_WORDS.has(word) ||
      MONTHS[word] !== undefined || WEEKDAYS[word] !== undefined ||
      word === 'unix' || word === 'date';
}

function isJoin(token: Token|undefined): boolean {
  const word = wordOf(token);
  return token?.type === 'symbol' && '+-,'.includes(token.text) ||
      word === 'to' || word === 'in';
}

// Sets aside an unfinished end, such as a last "+", or "- 5" before its unit
// is typed.
function takeAside(tokens: Token[]): string {
  let cut = tokens.length;
  if (isJoin(tokens[cut - 1])) {
    cut -= 1;
  } else if (tokens[cut - 1]?.type === 'number' && isJoin(tokens[cut - 2]) &&
             tokens[cut - 2]?.text !== ',') {
    cut -= 2;
  }
  return tokens.splice(cut).map(token => token.text).join(' ').replace(
      /-/g, '−');
}

// The hour, minute, and second of a clock time, or null when it cannot be.
function clockOf(clock: Extract<Token, {type: 'clock'}>):
    {hour: number, minute: number, second: number}|null {
  let hour = clock.hour;
  if (clock.half) {
    if (hour < 1 || hour > 12) {
      return null;
    }
    hour = hour % 12 + (clock.half === 'pm' ? 12 : 0);
  }
  if (hour > 23 || clock.minute > 59 || clock.second > 59) {
    return null;
  }
  return {hour, minute: clock.minute, second: clock.second};
}

function addTo(fields: Fields, unit: Field, amount: number) {
  let field = unit;
  let value = amount;
  while (!Number.isInteger(value)) {
    const smaller = SMALLER[field];
    if (!smaller) {
      throw new Unread();
    }
    field = smaller[0];
    value = field === 'milliseconds' ?
        Math.round(value * smaller[1]) :
        Math.round(value * smaller[1] * 1e6) / 1e6;
  }
  fields[field] = (fields[field] ?? 0) + value;
}

function hasTime(fields: Fields): boolean {
  return !!(fields.hours || fields.minutes || fields.seconds ||
            fields.milliseconds);
}

// The name of a zone: "Hong Kong" for Asia/Hong_Kong, or "UTC+8".
function zoneName(zone: string): string {
  const offset = zone.match(/^([+-])(\d{2}):(\d{2})$/);
  if (offset) {
    const [, sign, hours = '', minutes] = offset;
    return `UTC${sign}${+hours}${minutes === '00' ? '' : `:${minutes}`}`;
  }
  return zone.slice(zone.lastIndexOf('/') + 1).replace(/_/g, ' ');
}

// UTC and GMT, with an offset or without.
function offsetPlace(word: string): Place|null {
  const match = word.match(/^(utc|gmt)(?:([+-])(\d{1,2})(?::?(\d{2}))?)?$/);
  if (!match) {
    return null;
  }
  const [, base = '', sign, hours = '0', minutes = '00'] = match;
  if (+hours > 14 || +minutes > 59) {
    return null;
  }
  const zone = sign ?
      `${sign}${hours.padStart(2, '0')}:${minutes}` :
      'UTC';
  const name = sign ? zoneName(zone).replace('UTC', base.toUpperCase()) :
                      base.toUpperCase();
  return {zone, name, shown: name, label: name};
}

function zonesOf(region: string): string[] {
  const locale = new Intl.Locale(`und-${region}`) as Intl.Locale &
      {getTimeZones?: () => string[] | undefined};
  return locale.getTimeZones?.() ?? [];
}

function count(amount: number, one: string, many: string): string {
  return `${amount} ${Math.abs(amount) === 1 ? one : many}`;
}

// "3 h 24 min ago", or "in 1 h 40 min".
function distance(minutes: number): string {
  const size = Math.abs(minutes);
  const parts = [
    Math.floor(size / 60) ? `${Math.floor(size / 60)} h` : '',
    size % 60 ? `${size % 60} min` : '',
  ].filter(part => part).join(' ');
  return minutes < 0 ? `${parts} ago` : `in ${parts}`;
}

// "In 66 days", "In 3 weeks", or "66 days ago".
function daysAway(days: number): string {
  const weeks = days % 7 === 0;
  const text = weeks ? count(Math.abs(days) / 7, 'week', 'weeks') :
                       count(Math.abs(days), 'day', 'days');
  return days < 0 ? `${text} ago` : `In ${text}`;
}

// Puts a space before each piece after the first.
function spaced(ink: Ink[]): Ink[] {
  return ink.map((part, index) => index ? {...part, text: ` ${part.text}`} :
                                          part);
}

function textOf(ink: Ink[]): string {
  return ink.map(part => part.text).join('');
}

type Answer = Omit<Calculation, 'sum'|'sumName'|'why'|'note'>;

export class TimeMath {
  private readonly calculator_: Calculator;
  private readonly locale_: string|undefined;
  private zone_: string;
  private hour24_: boolean;
  private now_: () => number = () => Date.now();
  private places_: Map<string, Place>|null = null;

  constructor(calculator: Calculator, locale?: string) {
    this.calculator_ = calculator;
    this.locale_ = locale;
    this.zone_ = new Intl.DateTimeFormat().resolvedOptions().timeZone;
    this.hour24_ = !!new Intl.DateTimeFormat(locale, {hour: 'numeric'})
                         .resolvedOptions()
                         .hourCycle?.startsWith('h2');
  }

  // The Mac's time zone and 24-hour setting. Tests also set the time.
  setClock(zone: string, hour24: boolean, now?: () => number) {
    try {
      new Intl.DateTimeFormat(this.locale_, {timeZone: zone});
      this.zone_ = zone;
    } catch {
      // The page's own zone stays.
    }
    this.hour24_ = hour24;
    if (now) {
      this.now_ = now;
    }
  }

  // A card for text about time, or null when a word in it is not read.
  // Undefined for text with nothing about time in it, for the Calculator.
  calculate(text: string): Calculation|null|undefined {
    const tokens = tokenize(text);
    if (!tokens.some(isAboutTime)) {
      return undefined;
    }
    let reading: Reading;
    try {
      reading = this.read_(tokens);
    } catch {
      return null;
    }
    if (this.isBare_(reading)) {
      return null;
    }
    const sum = this.sum_(reading);
    try {
      return {...this.work_(reading), sum, why: '', note: '', live: true};
    } catch (error) {
      if (!(error instanceof NoAnswer)) {
        return null;
      }
      return {
        sum,
        sumName: this.sumName_(reading, null, false),
        answer: [],
        answerName: '',
        copy: '',
        why: error.message,
        note: '',
        live: true,
      };
    }
  }

  private read_(tokens: Token[]): Reading {
    const aside = takeAside(tokens);
    const reading: Reading = {terms: [], ops: [], target: null, aside};
    let at = 0;
    const next = () => {
      const [term, end] = this.term_(tokens, at);
      reading.terms.push(term);
      at = end;
    };
    next();
    while (at < tokens.length) {
      const token = tokens[at];
      const word = wordOf(token);
      if (token?.type === 'symbol' && (token.text === '+' || token.text === '-')) {
        reading.ops.push(token.text);
        at += 1;
        next();
      } else if (word === 'in' && this.duration_(tokens, at + 1)) {
        // "in" before a length of time adds it.
        reading.ops.push('+');
        reading.terms.push(this.inWords_(tokens, at));
        at = this.duration_(tokens, at + 1)?.[1] ?? tokens.length;
      } else if (word === 'to' || word === 'in') {
        reading.target = this.target_(tokens.slice(at + 1));
        at = tokens.length;
      } else {
        throw new Unread();
      }
    }
    return reading;
  }

  // "in 3 days" as a term whose sum reads "in 3 days".
  private inWords_(tokens: Token[], at: number): Term {
    const [term] = this.duration_(tokens, at + 1) ?? [];
    if (!term) {
      throw new Unread();
    }
    return {...term, ink: [{text: 'in', kind: 'operator'}, ...term.ink]};
  }

  private term_(tokens: Token[], at: number): [Term, number] {
    const moment = this.moment_(tokens, at);
    if (moment) {
      return moment;
    }
    const duration = this.duration_(tokens, at);
    if (duration) {
      return duration;
    }
    const token = tokens[at];
    if (token?.type !== 'number') {
      throw new Unread();
    }
    const number: Ink = {text: token.text, kind: 'number'};
    const unit = tokens[at + 1];
    const kind = unit?.type === 'word' ? this.calculator_.kindOf(unit.text) : '';
    if (unit && kind) {
      return [
        {type: 'amount', kind, ink: [number, {text: unit.text, kind: 'unit'}]},
        at + 2,
      ];
    }
    return [
      {type: 'number', text: token.text, value: token.value, ink: [number]},
      at + 1,
    ];
  }

  // A day word, a date, or a weekday, and a clock time, in either order, then
  // a place.
  private moment_(tokens: Token[], start: number): [Moment, number]|null {
    const moment: Moment = {type: 'moment', ink: []};
    let at = start;
    for (;;) {
      const token = tokens[at];
      const word = wordOf(token);
      const hasDay = !!(moment.day || moment.date || moment.weekday);
      const weekday = WEEKDAYS[wordOf(tokens[at + 1])];
      const date = hasDay ? null : this.date_(tokens, at);
      if (DAY_WORDS.has(word) && !hasDay) {
        moment.day = word;
        moment.ink.push({text: word, kind: 'unit'});
        at += 1;
      } else if ((word === 'next' || word === 'last') && weekday && !hasDay) {
        moment.weekday = {day: weekday, which: word};
        moment.ink.push(
            {text: word, kind: 'unit'},
            {text: this.weekdayName_(weekday), kind: 'number'});
        at += 2;
      } else if (WEEKDAYS[word] && !hasDay) {
        moment.weekday = {day: WEEKDAYS[word] ?? 0, which: ''};
        moment.ink.push(
            {text: this.weekdayName_(moment.weekday.day), kind: 'number'});
        at += 1;
      } else if (date) {
        moment.date = date[0];
        moment.ink.push({text: this.dateShown_(date[0]), kind: 'number'});
        at = date[1];
      } else if (token?.type === 'clock' && !moment.clock) {
        moment.clock = token;
        moment.ink.push({text: this.clockShown_(token), kind: 'number'});
        at += 1;
      } else if (token?.text === ',' && hasDay &&
                 tokens[at + 1]?.type === 'clock' && !moment.clock) {
        at += 1;
      } else {
        break;
      }
    }
    if (at === start) {
      return null;
    }
    if (moment.day === 'now' && moment.clock) {
      throw new Unread();
    }
    const place = this.place_(tokens, at);
    if (place) {
      moment.place = place[0];
      moment.ink.push({text: place[0].shown, kind: 'unit'});
      at = place[1];
    }
    return [moment, at];
  }

  // A month name and a day, in either order, with a year after them, or a
  // date written year first.
  private date_(tokens: Token[], at: number): [DateParts, number]|null {
    const token = tokens[at];
    if (token?.type === 'iso') {
      return [
        {year: token.year, month: token.month, day: token.day, text: token.text},
        at + 1,
      ];
    }
    const next = tokens[at + 1];
    const isDay = (part: Token|undefined) => part?.type === 'number' &&
        Number.isInteger(part.value) && part.text.length <= 2;
    let month = MONTHS[wordOf(token)];
    let day = isDay(next) ? next : undefined;
    if (!month || !day) {
      month = MONTHS[wordOf(next)];
      day = isDay(token) ? token : undefined;
    }
    if (!month || day?.type !== 'number') {
      return null;
    }
    let end = at + 2;
    const comma = tokens[end]?.text === ',' ? 1 : 0;
    const year = tokens[end + comma];
    const parts: DateParts = {month, day: day.value, text: ''};
    if (year?.type === 'number' && /^\d{4}$/.test(year.text)) {
      parts.year = year.value;
      end += comma + 1;
    }
    parts.text = this.dateName_(parts);
    return [parts, end];
  }

  // A length of time, such as "1 h 30 min".
  private duration_(tokens: Token[], start: number): [Term, number]|null {
    const fields: Fields = {};
    const ink: Ink[] = [];
    let at = start;
    for (;;) {
      const amount = tokens[at];
      const unit = tokens[at + 1];
      const field = DURATION_UNITS[wordOf(unit)];
      if (amount?.type !== 'number' || !unit || !field) {
        break;
      }
      addTo(fields, field, amount.value);
      ink.push(
          {text: amount.text, kind: 'number'}, {text: unit.text, kind: 'unit'});
      at += 2;
    }
    return at === start ? null : [{type: 'duration', fields, ink}, at];
  }

  private target_(tokens: Token[]): Target {
    const [first] = tokens;
    const word = wordOf(first);
    if (tokens.length === 1 && word === 'unix') {
      return {type: 'unix'};
    }
    if (tokens.length === 1 && word === 'date') {
      return {type: 'date'};
    }
    const place = this.place_(tokens, 0);
    if (place && place[1] === tokens.length) {
      return {type: 'place', place: place[0]};
    }
    if (tokens.length === 1 && first?.type === 'word' &&
        this.calculator_.kindOf(first.text)) {
      return {type: 'unit', word: first.text};
    }
    throw new Unread();
  }

  // The longest run of words, up to four, that names a place.
  private place_(tokens: Token[], start: number): [Place, number]|null {
    for (let length = Math.min(4, tokens.length - start); length > 0;
         length--) {
      const words = tokens.slice(start, start + length).map(wordOf);
      if (words.some(word => !word)) {
        continue;
      }
      const key = words.join(' ');
      const place = this.placeIndex_().get(key) ?? offsetPlace(key);
      if (place) {
        return [place, start + length];
      }
    }
    return null;
  }

  // Zone abbreviations, then the big cities, the cities that name zones, and
  // the countries with one zone. The first place to take a name keeps it.
  private placeIndex_(): Map<string, Place> {
    if (this.places_) {
      return this.places_;
    }
    const places = new Map<string, Place>();
    const add = (name: string, place: Place) => {
      const key = simplify(name);
      if (!places.has(key)) {
        places.set(key, place);
      }
    };
    const city = (name: string, zone: string): Place =>
        ({zone, name, shown: name, label: `${name} time`});
    for (const [abbreviation, zone, name] of ABBREVIATIONS) {
      add(abbreviation,
          {zone, name, shown: abbreviation, label: `${abbreviation} · ${name}`});
    }
    for (const [name, zone] of CITIES) {
      add(name, city(name, zone));
    }
    for (const zone of Intl.supportedValuesOf('timeZone')) {
      if (zone.includes('/') && !zone.startsWith('Etc/')) {
        add(zone, city(zoneName(zone), zone));
        add(zoneName(zone), city(zoneName(zone), zone));
      }
    }
    const regions =
        new Intl.DisplayNames(['en'], {type: 'region', fallback: 'none'});
    for (let first = 65; first <= 90; first++) {
      for (let second = 65; second <= 90; second++) {
        const code = String.fromCharCode(first, second);
        const name = regions.of(code);
        const [zone, other] = name ? zonesOf(code) : [];
        if (name && zone && !other) {
          add(name, city(name, zone));
        }
      }
    }
    this.places_ = places;
    return places;
  }

  // A bare `now`, or a bare weekday, may be a title: no card.
  private isBare_(reading: Reading): boolean {
    const [term] = reading.terms;
    if (reading.ops.length || reading.target || reading.aside ||
        term?.type !== 'moment' || term.clock || term.place || term.date) {
      return false;
    }
    return term.day === 'now' || term.weekday?.which === '';
  }

  private sum_(reading: Reading): Ink[] {
    const ink: Ink[] = [...(reading.terms[0]?.ink ?? [])];
    reading.ops.forEach((op, index) => {
      const term = reading.terms[index + 1];
      if (term?.ink[0]?.text !== 'in') {
        ink.push({text: op === '-' ? '−' : '+', kind: 'operator'});
      }
      ink.push(...(term?.ink ?? []));
    });
    if (reading.aside) {
      ink.push({text: reading.aside, kind: 'aside'});
    }
    return spaced(ink);
  }

  private work_(reading: Reading): Omit<Answer, 'live'>&{sumName: string} {
    const [first] = reading.terms;
    if (reading.target?.type === 'date') {
      if (first?.type !== 'number' || reading.terms.length > 1) {
        throw new Unread();
      }
      return this.timestamp_(first);
    }
    const values = reading.terms.map(term => this.value_(term));
    let value = values[0];
    reading.ops.forEach((op, index) => {
      const next = values[index + 1];
      if (value && next) {
        value = this.combine_(op, value, next);
      }
    });
    if (!value) {
      throw new Unread();
    }
    // A place after `now` shows the time there.
    const nowPlace = first?.type === 'moment' && first.day === 'now' &&
            first.place ?
        first.place :
        null;
    const target = reading.target ??
        (nowPlace ? {type: 'place' as const, place: nowPlace} : null);
    const firstMoment = values.find(found => found.type === 'moment') ?? null;
    const converted = target?.type === 'place' || target?.type === 'unix';
    const sumName = this.sumName_(reading, firstMoment, converted);
    return {sumName, ...this.answer_(value, target, reading)};
  }

  private answer_(value: Value, target: Target|null, reading: Reading):
      Omit<Answer, 'live'> {
    if (target?.type === 'place' || target?.type === 'unix') {
      if (value.type !== 'moment') {
        throw new NoAnswer(`Cannot convert ${this.kind_(value)} to ${
            target.type === 'place' ? 'a place' : 'Unix time'}`);
      }
      if (target.type === 'unix') {
        const seconds = String(Math.floor(value.at.epochMilliseconds / 1000));
        return {
          answer: [{text: seconds, kind: 'number'}],
          answerName: 'Unix time',
          copy: seconds,
        };
      }
      return this.momentAnswer_(
          value, target.place.zone, target.place.name, /*placed=*/ true);
    }
    if (target?.type === 'unit') {
      return this.unitAnswer_(value, target.word);
    }
    if (value.type === 'moment') {
      const placed = reading.terms.some(
          term => term.type === 'moment' && term.place);
      return this.momentAnswer_(
          value, this.zone_, zoneName(this.zone_), placed);
    }
    if (value.type === 'days' || value.type === 'span') {
      return this.gapAnswer_(value);
    }
    if (value.type === 'duration') {
      return this.gapAnswer_({type: 'span', ms: this.ms_(value)});
    }
    throw new Unread();
  }

  private value_(term: Term): Value {
    switch (term.type) {
      case 'moment':
        return this.momentValue_(term);
      case 'duration':
        return {type: 'duration', fields: term.fields};
      case 'amount':
        return {type: 'amount', kind: term.kind};
      default:
        return {type: 'number'};
    }
  }

  private momentValue_(term: Moment): Value {
    const zone = term.place?.zone ?? this.zone_;
    const now = this.nowIn_(zone);
    if (term.day === 'now') {
      return {type: 'moment', at: now, zone, time: true};
    }
    let date = now.toPlainDate();
    if (term.day === 'tomorrow') {
      date = date.add({days: 1});
    } else if (term.day === 'yesterday') {
      date = date.subtract({days: 1});
    } else if (term.date) {
      date = this.dateOf_(term.date, date);
    } else if (term.weekday) {
      date = weekdayDate(term.weekday, date);
    }
    let clock = {hour: 0, minute: 0, second: 0};
    if (term.clock) {
      const read = clockOf(term.clock);
      if (!read) {
        throw new NoAnswer(`${term.clock.shown} is not a time`);
      }
      clock = read;
    }
    const at = Temporal.ZonedDateTime.from({
      timeZone: zone,
      year: date.year,
      month: date.month,
      day: date.day,
      ...clock,
    });
    return {type: 'moment', at, zone, time: !!term.clock};
  }

  // A date with no year is the next one, today included.
  private dateOf_(parts: DateParts, today: Temporal.PlainDate):
      Temporal.PlainDate {
    const years = parts.year === undefined ?
        Array.from({length: 9}, (_, index) => today.year + index) :
        [parts.year];
    for (const year of years) {
      let date: Temporal.PlainDate;
      try {
        date = Temporal.PlainDate.from(
            {year, month: parts.month, day: parts.day}, {overflow: 'reject'});
      } catch {
        continue;
      }
      if (parts.year !== undefined ||
          Temporal.PlainDate.compare(date, today) >= 0) {
        return date;
      }
    }
    throw new NoAnswer(`${parts.text} is not a date`);
  }

  private combine_(op: '+'|'-', left: Value, right: Value): Value {
    if (left.type === 'moment' && right.type === 'duration') {
      return {
        ...left,
        at: op === '+' ? left.at.add(right.fields) :
                         left.at.subtract(right.fields),
        time: left.time || hasTime(right.fields),
      };
    }
    if (op === '+' && left.type === 'duration' && right.type === 'moment') {
      return this.combine_(op, right, left);
    }
    if (op === '-' && left.type === 'moment' && right.type === 'moment') {
      if (!left.time && !right.time) {
        const from = right.at.toPlainDate();
        const to = left.at.toPlainDate();
        return {type: 'days', days: from.until(to).days, from, to};
      }
      return {
        type: 'span',
        ms: left.at.epochMilliseconds - right.at.epochMilliseconds,
      };
    }
    const lengths = ['duration', 'days', 'span'];
    if (lengths.includes(left.type) && lengths.includes(right.type)) {
      const sign = op === '+' ? 1 : -1;
      const leftDays = wholeDays(left);
      const rightDays = wholeDays(right);
      if (leftDays !== null && rightDays !== null) {
        return {type: 'days', days: leftDays + sign * rightDays};
      }
      return {type: 'span', ms: this.ms_(left) + sign * this.ms_(right)};
    }
    throw new NoAnswer(
        op === '+' ?
            `Cannot add ${this.kind_(left)} to ${this.kind_(right)}` :
            `Cannot take ${this.kind_(right)} from ${this.kind_(left)}`);
  }

  // A length of time in milliseconds, with months and years counted from now.
  private ms_(value: Value): number {
    if (value.type === 'days') {
      return value.days * DAY_MS;
    }
    if (value.type === 'span') {
      return value.ms;
    }
    if (value.type === 'duration') {
      return Temporal.Duration.from(value.fields)
          .total({unit: 'milliseconds', relativeTo: this.nowIn_(this.zone_)});
    }
    throw new Unread();
  }

  private kind_(value: Value): string {
    switch (value.type) {
      case 'moment':
        return value.time ? 'a time' : 'a date';
      case 'amount':
        return value.kind;
      case 'number':
        return 'a number';
      default:
        return 'a length of time';
    }
  }

  private timestamp_(term: Extract<Term, {type: 'number'}>):
      Omit<Answer, 'live'>&{sumName: string} {
    if (!/^\d{1,13}$/.test(term.text)) {
      throw new Unread();
    }
    const ms = term.text.length === 13;
    const at = Temporal.Instant
                   .fromEpochMilliseconds(ms ? term.value : term.value * 1000)
                   .toZonedDateTimeISO(this.zone_);
    return {
      sumName: ms ? 'Unix time · ms' : 'Unix time',
      ...this.momentAnswer_(
          {type: 'moment', at, zone: this.zone_, time: true}, this.zone_,
          zoneName(this.zone_), /*placed=*/ false, /*full=*/ true),
    };
  }

  // The pill under the sum: what was read, and where.
  private sumName_(reading: Reading, first: Value|null, converted: boolean):
      string {
    const moments = reading.terms.filter(
        (term): term is Moment => term.type === 'moment');
    if (moments.length > 1) {
      return 'Expression';
    }
    const placed = moments.find(term => term.place && term.day !== 'now');
    if (placed?.place) {
      return placed.place.label;
    }
    if (converted && first?.type === 'moment') {
      if (!first.time) {
        return `Midnight, ${zoneName(this.zone_)}`;
      }
      const at = first.at.withTimeZone(this.zone_);
      return `${this.nearDay_(at, this.zone_) ?? this.weekday_(at)}, ${
          this.time_(at)}`;
    }
    if (moments.some(term => term.day || term.clock)) {
      return zoneName(this.zone_);
    }
    return 'Date';
  }

  // `placed` when a place was typed, so the pill names where the answer is.
  // `full` always shows the date with the time.
  private momentAnswer_(
      value: Extract<Value, {type: 'moment'}>, zone: string, name: string,
      placed: boolean, full = false): Omit<Answer, 'live'> {
    if (!value.time) {
      return this.dateAnswer_(value);
    }
    const at = value.at.withTimeZone(zone);
    const near = full ? null : this.nearDay_(at, zone);
    const text = near ? this.time_(at) : this.format_(at, {
      month: 'short',
      day: 'numeric',
      year: 'numeric',
      hour: 'numeric',
      minute: '2-digit',
      second: at.second ? '2-digit' : undefined,
    });
    let answerName = near ?? this.weekday_(at);
    if (placed) {
      answerName = `${answerName}, ${name}`;
    } else if (near) {
      const minutes = Math.round(
          (at.epochMilliseconds - this.nowIn_(zone).epochMilliseconds) / 60_000);
      answerName = minutes ?
          `${near}, ${distance(minutes)}` :
          this.format_(at, {weekday: 'long', month: 'short', day: 'numeric'});
    }
    return {
      answer: [{text, kind: 'number'}],
      answerName,
      copy: text,
      ...(near ? {} : {size: 'datetime' as const}),
    };
  }

  private dateAnswer_(value: Extract<Value, {type: 'moment'}>):
      Omit<Answer, 'live'> {
    const date = value.at.toPlainDate();
    const text = this.dateText_(
        date, {weekday: 'long', month: 'short', day: 'numeric', year: 'numeric'});
    const days = this.nowIn_(value.zone).toPlainDate().until(date).days;
    const answerName = days === 0 ? `Day ${date.dayOfYear} of ${date.year}` :
        days === 1                ? 'Tomorrow' :
        days === -1               ? 'Yesterday' :
                                    daysAway(days);
    return {
      answer: [{text, kind: 'number'}],
      answerName,
      copy: text,
      size: 'date',
    };
  }

  // A gap answers in days, or in hours and minutes, and keeps its sign.
  private gapAnswer_(value: Extract<Value, {type: 'days'|'span'}>):
      Omit<Answer, 'live'> {
    const parts: Array<[number, string]> = [];
    let answerName = 'Duration';
    if (value.type === 'days') {
      parts.push([value.days, Math.abs(value.days) === 1 ? 'day' : 'days']);
      answerName = this.daysName_(value) || answerName;
    } else {
      let rest = Math.round(Math.abs(value.ms) / 1000);
      const days = Math.floor(rest / 86_400);
      rest -= days * 86_400;
      const hours = Math.floor(rest / 3600);
      rest -= hours * 3600;
      const minutes = Math.floor(rest / 60);
      const units: Array<[number, string]> = [
        [days, days === 1 ? 'day' : 'days'],
        [hours, 'h'],
        [minutes, 'min'],
        [rest - minutes * 60, 's'],
      ];
      parts.push(...units.filter(([amount]) => amount));
      if (!parts.length) {
        parts.push([0, 'min']);
      }
      if (value.ms < 0 && parts[0]) {
        parts[0] = [-parts[0][0], parts[0][1]];
      }
    }
    const number = new Intl.NumberFormat(this.locale_);
    const ink = spaced(parts.flatMap(([amount, unit]): Ink[] => [
      {text: `${amount < 0 ? '−' : ''}${number.format(Math.abs(amount))}`,
       kind: 'number'},
      {text: unit, kind: 'unit'},
    ]));
    return {answer: ink, answerName, copy: textOf(ink)};
  }

  // The months and days in a gap between dates, or its weeks and days.
  private daysName_(value: Extract<Value, {type: 'days'}>): string {
    const sign = value.days < 0 ? '−' : '';
    const size = Math.abs(value.days);
    const gap = value.from && value.to ?
        value.from.until(value.to, {largestUnit: 'years'}) :
        null;
    if (gap && (gap.years || gap.months)) {
      return sign + [
        gap.years ? count(Math.abs(gap.years), 'year', 'years') : '',
        gap.months ? count(Math.abs(gap.months), 'month', 'months') : '',
        gap.days ? count(Math.abs(gap.days), 'day', 'days') : '',
      ].filter(part => part).join(' ');
    }
    if (size >= 7) {
      return sign + [
        count(Math.floor(size / 7), 'week', 'weeks'),
        size % 7 ? count(size % 7, 'day', 'days') : '',
      ].filter(part => part).join(' ');
    }
    return '';
  }

  private unitAnswer_(value: Value, word: string): Omit<Answer, 'live'> {
    const converted = value.type === 'days' ?
        this.calculator_.convertTime(value.days, 'day', word) :
        value.type === 'span' || value.type === 'duration' ?
        this.calculator_.convertTime(this.ms_(value) / 1000, 's', word) :
        `Cannot convert ${this.kind_(value)} to ${word}`;
    if (typeof converted === 'string') {
      throw new NoAnswer(converted);
    }
    return converted;
  }

  // Now, to the minute, so a card moves on with the clock.
  private nowIn_(zone: string): Temporal.ZonedDateTime {
    const minute = Math.floor(this.now_() / 60_000) * 60_000;
    return Temporal.Instant.fromEpochMilliseconds(minute).toZonedDateTimeISO(
        zone);
  }

  // Today, Tomorrow, or Yesterday where the time is shown, or null.
  private nearDay_(at: Temporal.ZonedDateTime, zone: string): string|null {
    const days = this.nowIn_(zone).toPlainDate().until(at.toPlainDate()).days;
    return days === 0 ? 'Today' :
        days === 1    ? 'Tomorrow' :
        days === -1   ? 'Yesterday' :
                        null;
  }

  private format_(
      at: Temporal.ZonedDateTime, options: Intl.DateTimeFormatOptions): string {
    return this.formatIn_(at.epochMilliseconds, this.zoneOf_(at), options);
  }

  private formatIn_(
      ms: number, zone: string, options: Intl.DateTimeFormatOptions): string {
    const clock: Intl.DateTimeFormatOptions =
        options.hour ? {hourCycle: this.hour24_ ? 'h23' : 'h12'} : {};
    // Intl puts a narrow no-break space before PM, which pastes badly.
    return new Intl.DateTimeFormat(
               this.locale_, {...options, ...clock, timeZone: zone})
        .format(ms)
        .replace(/[  ]/g, ' ');
  }

  // Temporal's zone name, written out for engines without timeZoneId.
  private zoneOf_(at: Temporal.ZonedDateTime): string {
    return at.toString({offset: 'never'}).match(/\[(.+)\]$/)?.[1] ?? 'UTC';
  }

  private time_(at: Temporal.ZonedDateTime): string {
    return this.format_(at, {
      hour: 'numeric',
      minute: '2-digit',
      second: at.second ? '2-digit' : undefined,
    });
  }

  private weekday_(at: Temporal.ZonedDateTime): string {
    return this.format_(at, {weekday: 'long'});
  }

  private weekdayName_(day: number): string {
    // Jan 1, 2024 was a Monday.
    return this.formatIn_(
        Date.UTC(2024, 0, day, 12), 'UTC', {weekday: 'long'});
  }

  private dateText_(
      date: {year: number, month: number, day: number},
      options: Intl.DateTimeFormatOptions): string {
    return this.formatIn_(
        Date.UTC(date.year, date.month - 1, date.day, 12), 'UTC', options);
  }

  // "Feb 30", as typed, even when no year has that day.
  private dateName_(parts: DateParts): string {
    const month = this.dateText_(
        {year: 2000, month: parts.month, day: 1}, {month: 'short'});
    return `${month} ${parts.day}${
        parts.year === undefined ? '' : `, ${parts.year}`}`;
  }

  private dateShown_(parts: DateParts): string {
    try {
      Temporal.PlainDate.from(
          {year: parts.year ?? 2000, month: parts.month, day: parts.day},
          {overflow: 'reject'});
    } catch {
      return parts.text;
    }
    return this.dateText_({...parts, year: parts.year ?? 2000}, {
      month: 'short',
      day: 'numeric',
      year: parts.year === undefined ? undefined : 'numeric',
    });
  }

  // 3 PM, or 15:00 with the Mac's 24-hour setting.
  private clockShown_(clock: Extract<Token, {type: 'clock'}>): string {
    const read = clockOf(clock);
    if (!read) {
      return clock.shown;
    }
    const exact = !this.hour24_ && !read.minute && !read.second;
    return this.formatIn_(
        Date.UTC(2000, 0, 1, read.hour, read.minute, read.second), 'UTC', {
          hour: 'numeric',
          minute: exact ? undefined : '2-digit',
          second: read.second ? '2-digit' : undefined,
        });
  }
}

// A length of time in whole days, or null when it has hours, months, or years.
function wholeDays(value: Value): number|null {
  if (value.type === 'days') {
    return value.days;
  }
  if (value.type !== 'duration' ||
      Object.keys(value.fields).some(
          field => field !== 'days' && field !== 'weeks')) {
    return null;
  }
  return (value.fields.days ?? 0) + 7 * (value.fields.weeks ?? 0);
}

// The weekday on or after today, or in next week, or the last before today.
// Weeks start on Monday.
function weekdayDate(
    weekday: {day: number, which: ''|'next'|'last'},
    today: Temporal.PlainDate): Temporal.PlainDate {
  if (weekday.which === 'next') {
    return today.add({days: 8 - today.dayOfWeek + weekday.day - 1});
  }
  if (weekday.which === 'last') {
    return today.subtract(
        {days: (today.dayOfWeek - weekday.day + 7) % 7 || 7});
  }
  return today.add({days: (weekday.day - today.dayOfWeek + 7) % 7});
}
