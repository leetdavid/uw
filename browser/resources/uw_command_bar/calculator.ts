// Copyright 2026 The uw Authors. All rights reserved.

// The command bar's calculator: mathjs works out math, units, and currencies,
// and uw reads the typed text for it. The page passes in mathjs, which it
// loads as a script; the tests pass in the npm package. The rules are in
// docs/plans/command-bar.md, under "Math, units, currency, and translation".

export interface MathNode {
  type: string;
  op?: string;
  fn?: string|MathNode;
  args?: MathNode[];
  implicit?: boolean;
  value?: unknown;
  name?: string;
  content?: MathNode;
  evaluate(): unknown;
  transform(callback: (node: MathNode) => MathNode): MathNode;
}

interface UnitPart {
  unit: {name: string, base: object};
  prefix: {name: string};
  power: number;
}

export interface MathUnit {
  units: UnitPart[];
  dimensions: number[];
  to(unit: string): MathUnit;
  toNumber(unit: string): number;
  equalBase(other: MathUnit): boolean;
}

interface MathJs {
  parse(text: string): MathNode;
  evaluate(text: string): unknown;
  unit(text: string): MathUnit;
  isUnit(value: unknown): boolean;
  format(value: unknown, options: {precision: number}): string;
  createUnit(
      name: string, definition?: string|{definition: string, aliases: string[]},
      options?: {override: boolean}): unknown;
  Unit: {
    isValuelessUnit(name: string): boolean,
    BASE_UNITS: Record<string, {dimensions: number[]}>,
    UNITS: Record<string, {base: object, value: unknown, offset: unknown}>,
  };
  SymbolNode: new (name: string) => MathNode;
  OperatorNode:
      new (op: string, fn: string, args: MathNode[], implicit: boolean) =>
          MathNode;
}

export interface MathJsLibrary {
  create(factories: unknown): MathJs;
  all: unknown;
}

export type InkKind = 'number'|'unit'|'operator'|'aside';

// One piece of the sum or the answer. The page draws numbers in full ink,
// units lighter, operators lightest, and what was set aside with an underline.
export interface Ink {
  text: string;
  kind: InkKind;
  // Drawn with no space before it.
  tight?: boolean;
}

export interface Calculation {
  // The sum as it was read, and the units it read, in words.
  sum: Ink[];
  sumName: string;
  // Empty when there is no answer.
  answer: Ink[];
  answerName: string;
  // What Return copies.
  copy: string;
  // Why there is no answer.
  why: string;
  // The exchange rates the answer used, and their date.
  note: string;
  // A long answer, a date or a date with a time, is drawn smaller.
  size?: 'date'|'datetime';
  // The answer moves with the clock, so the page works it out each minute.
  live?: boolean;
}

// The day's rates: how much of each currency one euro buys.
export interface Rates {
  date: string;
  perEuro: Record<string, number>;
}

// A currency symbol typed before or after an amount, and its code. `$` is
// always the US dollar.
const SYMBOLS: Record<string, string> = {
  'US$': 'USD',
  'HK$': 'HKD',
  'NZ$': 'NZD',
  'CN¥': 'CNY',
  'A$': 'AUD',
  'C$': 'CAD',
  'S$': 'SGD',
  '$': 'USD',
  '€': 'EUR',
  '£': 'GBP',
  '₩': 'KRW',
  '₹': 'INR',
  '¥': 'JPY',
};

const CONSTANTS = new Set(['pi', 'e', 'tau', 'phi']);

const OPERATORS: Record<string, string> = {
  '+': '+',
  '-': '−',
  '*': '×',
  '/': '÷',
  '%': '%',
  'mod': 'mod',
};

const PREFIXES: Record<string, string> = {
  Y: 'yotta',
  Z: 'zetta',
  E: 'exa',
  P: 'peta',
  T: 'tera',
  G: 'giga',
  M: 'mega',
  k: 'kilo',
  h: 'hecto',
  da: 'deca',
  d: 'deci',
  c: 'centi',
  m: 'milli',
  u: 'micro',
  n: 'nano',
  p: 'pico',
  f: 'femto',
  a: 'atto',
};

// The words for mathjs units whose names are symbols, singular and plural.
const WORDS: Record<string, [string, string]> = {
  m: ['meter', 'meters'],
  g: ['gram', 'grams'],
  t: ['tonne', 'tonnes'],
  s: ['second', 'seconds'],
  min: ['minute', 'minutes'],
  minute: ['minute', 'minutes'],
  h: ['hour', 'hours'],
  hr: ['hour', 'hours'],
  W: ['watt', 'watts'],
  Wh: ['watt-hour', 'watt-hours'],
  J: ['joule', 'joules'],
  N: ['newton', 'newtons'],
  A: ['amp', 'amps'],
  Ah: ['amp-hour', 'amp-hours'],
  C: ['coulomb', 'coulombs'],
  V: ['volt', 'volts'],
  Pa: ['pascal', 'pascals'],
  Hz: ['hertz', 'hertz'],
  l: ['liter', 'liters'],
  L: ['liter', 'liters'],
  mi: ['mile', 'miles'],
  ft: ['foot', 'feet'],
  foot: ['foot', 'feet'],
  feet: ['foot', 'feet'],
  in: ['inch', 'inches'],
  inch: ['inch', 'inches'],
  yd: ['yard', 'yards'],
  lb: ['pound', 'pounds'],
  lbs: ['pound', 'pounds'],
  oz: ['ounce', 'ounces'],
  floz: ['fluid ounce', 'fluid ounces'],
  gal: ['gallon', 'gallons'],
  cup: ['cup', 'cups'],
  tsp: ['teaspoon', 'teaspoons'],
  tbsp: ['tablespoon', 'tablespoons'],
  degC: ['degree Celsius', 'degrees Celsius'],
  degF: ['degree Fahrenheit', 'degrees Fahrenheit'],
  K: ['kelvin', 'kelvins'],
  mph: ['mile per hour', 'miles per hour'],
  kph: ['kilometer per hour', 'kilometers per hour'],
  cal: ['calorie', 'calories'],
  ha: ['hectare', 'hectares'],
  b: ['bit', 'bits'],
  B: ['byte', 'bytes'],
  deg: ['degree', 'degrees'],
  rad: ['radian', 'radians'],
};

const SYMBOL_TEXT: Record<string, string> = {
  minute: 'min',
  degC: '°C',
  degF: '°F',
  deg: '°',
};

const IMPERIAL = new Set([
  'in',     'inch',  'inches', 'ft',     'foot',       'feet',
  'yd',     'yard',  'yards',  'mi',     'mile',       'miles',
  'lb',     'lbs',   'lbm',    'oz',     'ounce',      'ounces',
  'gal',    'gallon', 'gallons', 'floz', 'fluidounce', 'fluidounces',
  'degF',   'fahrenheit', 'mph',
]);

const COOKING = new Set(['cup', 'cups', 'tsp', 'tbsp', 'teaspoon', 'tablespoon']);

// What the base of a unit is called in "Cannot add a mass to a length".
const KINDS: Record<string, string> = {
  MASS: 'a mass',
  LENGTH: 'a length',
  TIME: 'a time',
  CURRENT: 'a current',
  TEMPERATURE: 'a temperature',
  FORCE: 'a force',
  SURFACE: 'an area',
  VOLUME: 'a volume',
  ENERGY: 'an energy',
  POWER: 'a power',
  PRESSURE: 'a pressure',
  ELECTRIC_CHARGE: 'a charge',
  ELECTRIC_POTENTIAL: 'a voltage',
  FREQUENCY: 'a frequency',
  ANGLE: 'an angle',
  BIT: 'an amount of data',
  EUR_STUFF: 'money',
};

const SUPERSCRIPT = '⁰¹²³⁴⁵⁶⁷⁸⁹';

function superscript(power: number): string {
  if (power === 1) {
    return '';
  }
  const digits = String(Math.abs(power))
                     .split('')
                     .map(digit => SUPERSCRIPT[Number(digit)] ?? digit)
                     .join('');
  return (power < 0 ? '⁻' : '') + digits;
}

function escapeForRegExp(text: string): string {
  return text.replace(/[$()*+.?[\\\]^{|}]/g, '\\$&');
}

const SYMBOL_PATTERN =
    Object.keys(SYMBOLS).map(escapeForRegExp).join('|');
const AMOUNT = '\\d[\\d,]*(?:\\.\\d+)?|\\.\\d+';
const SYMBOL_FIRST = new RegExp(`(${SYMBOL_PATTERN})\\s*(${AMOUNT})`, 'g');
const SYMBOL_AFTER = new RegExp(`(${AMOUNT})\\s*(${SYMBOL_PATTERN})`, 'g');

// Turns what was typed into what mathjs reads: currency symbols into codes,
// "as" into "to", and × and ÷ into * and /.
function normalize(text: string): string {
  let out = text.replace(/[×✕]/g, '*').replace(/÷/g, '/').replace(/−/g, '-');
  out = out.replace(
      SYMBOL_FIRST,
      (_match, symbol: string, amount: string) =>
          `${amount} ${SYMBOLS[symbol] ?? symbol}`);
  out = out.replace(
      SYMBOL_AFTER,
      (_match, amount: string, symbol: string) =>
          `${amount} ${SYMBOLS[symbol] ?? symbol}`);
  out = out.replace(/\bas\b/g, 'to');
  if (!out.includes('(')) {
    out = out.replace(/(\d),(?=\d{3}(?!\d))/g, (_match, digit: string) => digit);
  }
  return out;
}

// The end of a half-typed sum that is set aside: an operator, or a "to".
const UNFINISHED = /\s*(?:[-+*/^,(%]|\bto|\bin)\s*$/;

interface Reading {
  ink: Ink[];
  // The units read, in words, each once.
  words: string[];
  // Explicit operators, functions, and conversions.
  operators: number;
  units: number;
  constants: number;
  // Each amount typed in a currency.
  amounts: Array<{value: number, code: string}>;
  // Symbols that mathjs reads differently from what uw reads.
  replace: Map<MathNode, MathNode>;
}

function emptyReading(): Reading {
  return {
    ink: [],
    words: [],
    operators: 0,
    units: 0,
    constants: 0,
    amounts: [],
    replace: new Map(),
  };
}

export class Calculator {
  private readonly math_: MathJs;
  private readonly locale_: string|undefined;
  private rates_: Rates|null = null;
  private currency_ = 'USD';

  constructor(library: MathJsLibrary, locale?: string) {
    this.math_ = library.create(library.all);
    this.locale_ = locale;
    const math = this.math_;
    // A cup is always the metric cup.
    math.createUnit(
        'cup', {definition: '250 ml', aliases: ['cups']}, {override: true});
    math.createUnit('tsp', '5 ml');
    math.createUnit('tbsp', '15 ml');
    math.createUnit('mph', '1 mi/h');
    math.createUnit('kph', '1 km/h');
    math.createUnit('EUR');
  }

  // `currency` is the one a bare amount converts to.
  setCurrencies(rates: Rates|null, currency: string) {
    this.currency_ = currency;
    this.rates_ = rates;
    for (const [code, rate] of Object.entries(rates?.perEuro ?? {})) {
      if (code !== 'EUR' && rate > 0 && /^[A-Z]{3}$/.test(code)) {
        this.math_.createUnit(code, `${1 / rate} EUR`, {override: true});
      }
    }
  }

  calculate(text: string): Calculation|null {
    if (!/\d/.test(text)) {
      return null;
    }
    const parsed = this.parse_(normalize(text));
    if (!parsed) {
      return null;
    }
    const reading = emptyReading();
    let target: MathNode|null = null;
    try {
      target = this.read_(parsed.node, reading);
    } catch {
      return null;
    }
    if (parsed.aside) {
      reading.ink.push({text: inkText(parsed.aside), kind: 'aside'});
    }
    if (reading.operators === 0 && reading.units === 0 &&
        reading.constants === 0) {
      return null;
    }
    const sumName = reading.words.length ? reading.words.join(', ') :
                                           'Expression';
    const looksLikeSum = reading.operators > 0 && reading.units > 0;
    const noAnswer = (why: string): Calculation|null => looksLikeSum ? {
      sum: spaced(reading.ink),
      sumName,
      answer: [],
      answerName: '',
      copy: '',
      why,
      note: '',
    } :
                                                                        null;
    const node = parsed.node.transform(n => reading.replace.get(n) ?? n);
    let value: unknown;
    try {
      value = node.evaluate();
    } catch (error) {
      return /do not match/.test(String(error)) ?
          noAnswer(this.misfit_(node)) :
          null;
    }
    const answer = this.answer_(value, {
      reading,
      converted: target !== null,
      bare: reading.operators === 0 && !parsed.aside,
    });
    if (!answer) {
      return null;
    }
    return {
      sum: spaced(reading.ink),
      sumName,
      why: '',
      ...answer,
      answer: spaced(answer.answer),
    };
  }

  // What a unit measures, as in "a mass" for kg, or "" for a word that is not
  // a unit.
  kindOf(word: string): string {
    const code = word.toUpperCase();
    if (code.length === 3 && this.isCurrency_(code)) {
      return KINDS['EUR_STUFF'] ?? '';
    }
    const name = this.unitName_(word);
    return name ? this.kindOfUnit_(this.math_.unit(name)) : '';
  }

  // A length of time, such as 66 days, in `target`, a unit that kindOf knows.
  // A target that is not a time says why it does not fit.
  convertTime(amount: number, unit: string, target: string):
      Pick<Calculation, 'answer'|'answerName'|'copy'>|string {
    let value: MathUnit;
    try {
      value = this.math_.unit(`${amount} ${unit}`).to(this.unitName_(target));
    } catch {
      return `Cannot convert a length of time to ${this.kindOf(target)}`;
    }
    const answer = this.answer_(
        value, {reading: emptyReading(), converted: true, bare: false});
    if (!answer) {
      return 'This length of time cannot be shown';
    }
    return {...answer, answer: spaced(answer.answer)};
  }

  // Parses the text, setting aside an unfinished end until what is left
  // parses. Unclosed brackets are closed.
  private parse_(text: string): {node: MathNode, aside: string}|null {
    let body = text.trim();
    let aside = '';
    for (let tries = 0; tries < 4 && body; tries++) {
      const open = (body.match(/\(/g) ?? []).length -
          (body.match(/\)/g) ?? []).length;
      try {
        return {
          node: this.math_.parse(body + ')'.repeat(Math.max(open, 0))),
          aside,
        };
      } catch {
        const end = body.match(UNFINISHED)?.[0];
        if (!end) {
          return null;
        }
        aside = end + aside;
        body = body.slice(0, body.length - end.length);
      }
    }
    return null;
  }

  // Reads `node` into `reading`, and returns the unit it converts to, if any.
  private read_(node: MathNode, reading: Reading): MathNode|null {
    if (node.type === 'OperatorNode' && node.fn === 'to') {
      const [from, to] = node.args ?? [];
      if (!from || !to) {
        throw new Error('to');
      }
      this.readNode_(from, reading);
      reading.operators++;
      const ignored: Reading = {...reading, ink: [], words: []};
      this.readNode_(to, ignored);
      return to;
    }
    this.readNode_(node, reading);
    return null;
  }

  private readNode_(node: MathNode, reading: Reading) {
    const math = this.math_;
    const args = node.args ?? [];
    switch (node.type) {
      case 'ConstantNode':
        if (typeof node.value !== 'number') {
          throw new Error('constant');
        }
        reading.ink.push({text: String(node.value), kind: 'number'});
        return;
      case 'SymbolNode':
        this.readSymbol_(node, reading);
        return;
      case 'ParenthesisNode':
        if (!node.content) {
          throw new Error('parenthesis');
        }
        reading.ink.push({text: '(', kind: 'operator'});
        this.readNode_(node.content, reading);
        reading.ink.push({text: ')', kind: 'operator'});
        return;
      case 'FunctionNode': {
        const name = typeof node.fn === 'object' ? node.fn.name ?? '' : '';
        if (!name || typeof (math as unknown as Record<string, unknown>)[name] !==
                'function') {
          throw new Error('function');
        }
        reading.operators++;
        reading.ink.push({text: name, kind: 'unit'});
        reading.ink.push({text: '(', kind: 'operator', tight: true});
        args.forEach((arg, index) => {
          if (index) {
            reading.ink.push({text: ',', kind: 'operator'});
          }
          this.readNode_(arg, reading);
        });
        reading.ink.push({text: ')', kind: 'operator'});
        return;
      }
      case 'OperatorNode':
        this.readOperator_(node, reading);
        return;
      default:
        throw new Error(node.type);
    }
  }

  private readOperator_(node: MathNode, reading: Reading) {
    const args = node.args ?? [];
    const [left, right] = args;
    if (node.fn === 'unaryMinus' || node.fn === 'unaryPlus') {
      if (!left) {
        throw new Error('unary');
      }
      reading.ink.push({text: node.fn === 'unaryMinus' ? '−' : '+',
                        kind: 'operator'});
      this.readNode_(left, reading);
      return;
    }
    if (!left || !right || args.length !== 2) {
      if (node.op === '!' && left) {
        this.readNode_(left, reading);
        reading.operators++;
        reading.ink.push({text: '!', kind: 'operator'});
        return;
      }
      throw new Error('operator');
    }
    if (node.implicit) {
      this.readNode_(left, reading);
      this.readNode_(right, reading);
      const code = right.type === 'SymbolNode' ?
          reading.replace.get(right)?.name ?? right.name ?? '' :
          '';
      if (left.type === 'ConstantNode' && typeof left.value === 'number' &&
          this.isCurrency_(code)) {
        reading.amounts.push({value: left.value, code});
      }
      return;
    }
    const shown = node.op === '^' ? '^' : OPERATORS[node.op ?? ''];
    if (!shown) {
      throw new Error('operator');
    }
    this.readNode_(left, reading);
    // A unit to a power reads as one unit: m^2 is m².
    const last = reading.ink[reading.ink.length - 1];
    if (node.op === '^' && left.type === 'SymbolNode' && last?.kind === 'unit' &&
        right.type === 'ConstantNode' && Number.isInteger(right.value)) {
      last.text += superscript(right.value as number);
      return;
    }
    reading.operators++;
    reading.ink.push({text: shown, kind: 'operator'});
    this.readNode_(right, reading);
  }

  private readSymbol_(node: MathNode, reading: Reading) {
    const math = this.math_;
    const name = node.name ?? '';
    const unit = (text: string, canonical: string) => {
      reading.units++;
      reading.ink.push({text, kind: 'unit'});
      this.addWords_(canonical, reading);
      if (canonical !== name) {
        reading.replace.set(node, new math.SymbolNode(canonical));
      }
    };
    const code = name.toUpperCase();
    if (code.length === 3 && this.isCurrency_(code)) {
      unit(code, code);
      return;
    }
    const canonical = this.unitName_(name);
    if (canonical) {
      unit(name, canonical);
      return;
    }
    if (CONSTANTS.has(name)) {
      reading.constants++;
      reading.ink.push({text: name, kind: 'number'});
      return;
    }
    // Units run together read as their product: Wm is W·m.
    if (/^[A-Za-z]+$/.test(name)) {
      for (let split = 1; split < name.length; split++) {
        const first = name.slice(0, split);
        const second = name.slice(split);
        if (this.isUnit_(first) && this.isUnit_(second)) {
          reading.units += 2;
          reading.ink.push({text: first, kind: 'unit'});
          reading.ink.push({text: second, kind: 'unit'});
          this.addWords_(first, reading);
          this.addWords_(second, reading);
          reading.replace.set(
              node,
              new math.OperatorNode(
                  '*', 'multiply',
                  [new math.SymbolNode(first), new math.SymbolNode(second)],
                  true));
          return;
        }
      }
    }
    throw new Error('unit');
  }

  private isUnit_(name: string): boolean {
    return name !== 'min' && this.math_.Unit.isValuelessUnit(name);
  }

  // The mathjs name of a unit word: min is minutes, and spelled-out names
  // ignore case while symbols keep it. "" when the word is not a unit.
  private unitName_(word: string): string {
    if (word === 'min') {
      return 'minute';
    }
    if (this.isUnit_(word)) {
      return word;
    }
    return word.length > 2 && this.isUnit_(word.toLowerCase()) ?
        word.toLowerCase() :
        '';
  }

  private kindOfUnit_(unit: MathUnit): string {
    for (const [base, name] of Object.entries(KINDS)) {
      const known = this.math_.Unit.BASE_UNITS[base]?.dimensions;
      if (known &&
          known.every((power, index) => power === unit.dimensions[index])) {
        return name;
      }
    }
    return this.unitWords_(unit, /*plural=*/ true);
  }

  private isCurrency_(code: string): boolean {
    return this.rates_ !== null &&
        (code === 'EUR' || this.rates_.perEuro[code] !== undefined);
  }

  private addWords_(unit: string, reading: Reading) {
    const words = this.unitWords_(this.math_.unit(unit), /*plural=*/ true);
    if (words && !reading.words.includes(words)) {
      reading.words.push(words);
    }
  }

  private answer_(value: unknown, how: {
    reading: Reading,
    converted: boolean,
    bare: boolean,
  }): Omit<Calculation, 'sum'|'sumName'|'why'>|null {
    const math = this.math_;
    if (typeof value === 'number') {
      if (!Number.isFinite(value)) {
        return null;
      }
      const text = this.number_(value, 10);
      return {
        answer: [{text, kind: 'number'}],
        answerName: 'Result',
        copy: text,
        note: '',
      };
    }
    if (!math.isUnit(value)) {
      return null;
    }
    let unit = value as MathUnit;
    const code = this.currencyOf_(unit);
    if (code) {
      let to = code;
      if (!how.converted) {
        to = how.bare && code === this.currency_ ?
            (code === 'USD' ? 'EUR' : 'USD') :
            this.currency_;
      }
      return this.money_(unit.toNumber(to), to, how.reading);
    }
    if (!how.converted) {
      if (how.bare) {
        const other = this.otherSystem_(unit);
        if (!other) {
          return null;
        }
        unit = unit.to(other);
      } else {
        unit = this.namedUnit_(unit);
      }
    }
    const formatted = math.format(unit, {precision: 14});
    const space = formatted.indexOf(' ');
    if (space < 0) {
      return null;
    }
    const amount = Number(formatted.slice(0, space));
    const shownUnit = math.evaluate(`1 ${formatted.slice(space + 1)}`);
    if (!Number.isFinite(amount) || !math.isUnit(shownUnit)) {
      return null;
    }
    const parts = (shownUnit as MathUnit).units;
    const number = this.number_(amount, 3);
    const unitText = this.unitSymbols_(parts, amount !== 1);
    return {
      answer: [{text: number, kind: 'number'}, {text: unitText, kind: 'unit'}],
      answerName: sentenceCase(this.unitWords_(shownUnit as MathUnit, amount !== 1)),
      copy: `${number} ${unitText}`,
      note: '',
    };
  }

  // Typed units that multiply into a unit with its own name stay in it, and a
  // power or a current times a time answers in watt-hours or amp-hours.
  private namedUnit_(unit: MathUnit): MathUnit {
    const math = this.math_;
    const parts = unit.units;
    if (parts.length > 1 && parts.every(part => part.power === 1)) {
      const joined =
          parts.map(part => part.prefix.name + part.unit.name).join('');
      if (this.isUnit_(joined) && unit.equalBase(math.unit(joined))) {
        return unit.to(joined);
      }
    }
    const bases = math.Unit.BASE_UNITS;
    for (const [source, named] of [['POWER', 'Wh'], ['CURRENT', 'Ah']] as const) {
      const from = parts.find(part => part.unit.base === bases[source]);
      if (from && unit.equalBase(math.unit(named)) &&
          parts.some(part => part.unit.base === bases['TIME'])) {
        const prefix = Object.entries(PREFIXES).find(
                           ([, long]) => long === from.prefix.name)?.[0] ??
            from.prefix.name;
        return unit.to(prefix + named);
      }
    }
    return unit;
  }

  // A bare amount converts to the other measurement system.
  private otherSystem_(unit: MathUnit): string {
    const part = unit.units[0];
    if (!part || unit.units.length !== 1 || part.power !== 1) {
      return '';
    }
    const name = part.unit.name;
    const imperial = IMPERIAL.has(name);
    const bases = this.math_.Unit.BASE_UNITS;
    if (name === 'mph') {
      return 'km/h';
    }
    if (name === 'kph') {
      return 'mph';
    }
    switch (part.unit.base) {
      case bases['LENGTH']: {
        const meters = unit.toNumber('m');
        if (imperial) {
          return meters >= 1000 ? 'km' : meters >= 1 ? 'm' : 'cm';
        }
        return meters >= 1000 ? 'mi' : meters >= 1 ? 'ft' : 'in';
      }
      case bases['MASS']: {
        const kilograms = unit.toNumber('kg');
        if (imperial) {
          return kilograms >= 1 ? 'kg' : 'g';
        }
        return kilograms >= 1 ? 'lb' : 'oz';
      }
      case bases['VOLUME']: {
        const liters = unit.toNumber('l');
        if (imperial) {
          return liters >= 1 ? 'l' : 'ml';
        }
        if (COOKING.has(name)) {
          return 'ml';
        }
        return liters >= 1 ? 'gal' : 'floz';
      }
      case bases['TEMPERATURE']:
        return name === 'degF' || name === 'fahrenheit' ? 'degC' : 'degF';
      default:
        return '';
    }
  }

  private currencyOf_(unit: MathUnit): string {
    const part = unit.units[0];
    if (!part || unit.units.length !== 1 || part.power !== 1 ||
        part.prefix.name) {
      return '';
    }
    return this.isCurrency_(part.unit.name) ? part.unit.name : '';
  }

  private money_(amount: number, code: string, reading: Reading):
      Omit<Calculation, 'sum'|'sumName'|'why'>|null {
    if (!Number.isFinite(amount)) {
      return null;
    }
    const answer = this.moneyParts_(amount, code);
    const copy = answer.map(ink => ink.text).join('');
    // A sum of several amounts says what each one came to.
    const notes = reading.amounts.length < 2 ?
        [] :
        reading.amounts.filter(typed => typed.code !== code).map(typed => {
          const converted = this.convert_(typed.value, typed.code, code);
          return `${this.moneyText_(typed.value, typed.code)} = ${
              this.moneyText_(converted, code)}`;
        });
    const date = this.rates_ ? this.date_(this.rates_.date) : '';
    if (date) {
      notes.push(`ECB rates of ${date}`);
    }
    return {
      answer,
      answerName: sentenceCase(this.currencyName_(code)),
      copy,
      note: notes.join(' · '),
    };
  }

  private convert_(amount: number, from: string, to: string): number {
    const perEuro = this.rates_?.perEuro ?? {};
    const rate = (code: string) => code === 'EUR' ? 1 : perEuro[code] ?? NaN;
    return amount / rate(from) * rate(to);
  }

  private moneyFormat_(amount: number, code: string): Intl.NumberFormat {
    const whole = Math.abs(amount - Math.round(amount)) < 0.005;
    return new Intl.NumberFormat(this.locale_, {
      style: 'currency',
      currency: code,
      minimumFractionDigits: whole ? 0 : undefined,
      maximumFractionDigits: whole ? 0 : 2,
    });
  }

  private moneyParts_(amount: number, code: string): Ink[] {
    const ink: Ink[] = [];
    for (const part of this.moneyFormat_(amount, code).formatToParts(amount)) {
      const kind: InkKind = part.type === 'currency' ? 'unit' : 'number';
      const last = ink[ink.length - 1];
      if (last && last.kind === kind) {
        last.text += part.value;
      } else {
        ink.push({text: part.value, kind, tight: ink.length > 0});
      }
    }
    return ink;
  }

  private moneyText_(amount: number, code: string): string {
    return this.moneyFormat_(amount, code).format(amount);
  }

  private currencyName_(code: string): string {
    const name = new Intl.NumberFormat(this.locale_, {
                   style: 'currency',
                   currency: code,
                   currencyDisplay: 'name',
                 })
                     .formatToParts(2)
                     .find(part => part.type === 'currency')
                     ?.value ??
        code;
    return name;
  }

  // The symbol of a unit typed by its name, as in m for "meters".
  private symbolOf_(name: string): string {
    const units = this.math_.Unit.UNITS;
    const unit = units[name];
    if (!unit) {
      return name;
    }
    let shortest = name;
    for (const other of Object.keys(units)) {
      if (other.length < shortest.length && this.sameUnit_(name, other)) {
        shortest = other;
      }
    }
    return shortest;
  }

  private sameUnit_(name: string, other: string): boolean {
    const units = this.math_.Unit.UNITS;
    const unit = units[name];
    const def = units[other];
    return !!unit && !!def && def.base === unit.base &&
        String(def.value) === String(unit.value) &&
        String(def.offset) === String(unit.offset);
  }

  // A unit spelled as a word, such as "week", takes its plural.
  private unitSymbols_(parts: UnitPart[], plural: boolean): string {
    const symbol = (part: UnitPart, power: number, many = false) => {
      const short = Object.entries(PREFIXES).find(
                        ([, name]) => name === part.prefix.name)?.[0];
      const prefix = short ?? part.prefix.name;
      const name = this.symbolOf_(part.unit.name);
      const text = many && !WORDS[name] && this.sameUnit_(name, `${name}s`) ?
          `${name}s` :
          SYMBOL_TEXT[name] ?? name;
      return (prefix === 'u' ? 'µ' : prefix) + text + superscript(power);
    };
    const above = parts.filter(part => part.power > 0);
    const below = parts.filter(part => part.power < 0);
    if (!above.length) {
      return below.map(part => symbol(part, part.power)).join('·');
    }
    const top = above
                    .map((part, index) => symbol(
                             part, part.power,
                             plural && index === above.length - 1))
                    .join('·');
    if (!below.length) {
      return top;
    }
    const bottom = below.map(part => symbol(part, -part.power)).join('·');
    return below.length > 1 ? `${top}/(${bottom})` : `${top}/${bottom}`;
  }

  private date_(date: string): string {
    const time = Date.parse(`${date}T12:00:00Z`);
    if (Number.isNaN(time)) {
      return '';
    }
    return new Intl.DateTimeFormat(this.locale_, {
             day: 'numeric',
             month: 'short',
             year: 'numeric',
             timeZone: 'UTC',
           })
        .format(time);
  }

  private number_(value: number, digits: number): string {
    const size = Math.abs(value);
    const scientific = size >= 1e15 || (size > 0 && size < 1e-6);
    return new Intl.NumberFormat(this.locale_, {
             maximumSignificantDigits: Number.isInteger(value) ? 21 : digits,
             notation: scientific ? 'scientific' : 'standard',
           })
        .format(value);
  }

  // The words for a unit: "kilograms", or "meters per second squared".
  private unitWords_(unit: MathUnit, plural: boolean): string {
    const code = this.currencyOf_(unit);
    if (code) {
      return this.currencyName_(code);
    }
    const word = (part: UnitPart, many: boolean) => {
      const prefix = PREFIXES[part.prefix.name] ?? part.prefix.name;
      const known = WORDS[this.symbolOf_(part.unit.name)];
      const base = known ? known[many ? 1 : 0] : part.unit.name;
      return prefix + base;
    };
    const powered = (part: UnitPart, many: boolean, after: boolean) => {
      const power = Math.abs(part.power);
      const text = word(part, many);
      if (power === 1) {
        return text;
      }
      if (after) {
        return power === 2 ? `${text} squared` :
            power === 3    ? `${text} cubed` :
                             `${text} to the ${power}`;
      }
      return power === 2 ? `square ${text}` :
          power === 3    ? `cubic ${text}` :
                           `${text} to the ${power}`;
    };
    const above = unit.units.filter(part => part.power > 0);
    const below = unit.units.filter(part => part.power < 0);
    const top = above
                    .map((part, index) => powered(
                             part, plural && index === above.length - 1,
                             /*after=*/ false))
                    .join('-');
    const bottom = below.map(part => powered(part, false, /*after=*/ true))
                       .join('-');
    return top && bottom ? `${top} per ${bottom}` :
        bottom           ? `per ${bottom}` :
                           top;
  }

  // Names the kinds of the first sum or conversion whose units do not fit.
  private misfit_(node: MathNode): string {
    const math = this.math_;
    let reason = 'These units do not fit together';
    const kind = (part: MathNode): string => {
      let value: unknown;
      try {
        value = part.evaluate();
      } catch {
        return '';
      }
      if (typeof value === 'number') {
        return 'a number';
      }
      return math.isUnit(value) ? this.kindOfUnit_(value as MathUnit) : '';
    };
    const visit = (part: MathNode): boolean => {
      const [left, right] = part.args ?? [];
      if (part.type === 'OperatorNode' && left && right &&
          (part.fn === 'add' || part.fn === 'subtract' || part.fn === 'to')) {
        const first = kind(left);
        const second = kind(right);
        if (first && second && first !== second) {
          reason = part.fn === 'add' ? `Cannot add ${first} to ${second}` :
              part.fn === 'subtract' ?
                                       `Cannot take ${second} from ${first}` :
                                       `Cannot convert ${first} to ${second}`;
          return true;
        }
      }
      return [...(part.args ?? []), ...(part.content ? [part.content] : [])]
          .some(visit);
    };
    visit(node);
    return reason;
  }
}

// Puts a space between the pieces, except inside brackets and after a
// function name.
function spaced(ink: Ink[]): Ink[] {
  return ink.map((part, index) => {
    const before = ink[index - 1];
    const tight = !before || part.tight || before.text === '(' ||
        part.text === ')' || part.text === ',' || part.text === '!';
    return tight ? {text: part.text, kind: part.kind} :
                   {text: ` ${part.text}`, kind: part.kind};
  });
}

function inkText(text: string): string {
  return text.trim().replace(/\*/g, '×').replace(/\//g, '÷').replace(/-/g, '−');
}

function sentenceCase(text: string): string {
  return text.charAt(0).toUpperCase() + text.slice(1);
}
