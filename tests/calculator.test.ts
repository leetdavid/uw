import assert from "node:assert/strict";
import test from "node:test";
import * as mathjs from "mathjs";
import { Calculator, type Calculation, type MathJsLibrary } from "../browser/resources/uw_command_bar/calculator.ts";

const RATES = { date: "2026-10-15", perEuro: { USD: 1.08, HKD: 8.4, GBP: 0.85, JPY: 160, KRW: 1450 } };

function calculator(currency = "HKD"): Calculator {
  const calculator = new Calculator(mathjs as unknown as MathJsLibrary, "en-US");
  calculator.setCurrencies(RATES, currency);
  return calculator;
}

function words(ink: Calculation["sum"]): string {
  return ink.map(part => part.text).join("");
}

function answer(text: string, currency?: string): string | undefined {
  const result = calculator(currency).calculate(text);
  return result && !result.why ? words(result.answer) : undefined;
}

test("plain math shows up to 10 significant digits", () => {
  assert.equal(answer("2*(3+4)"), "14");
  assert.equal(answer("2/3"), "0.6666666667");
  assert.equal(answer("5 * pi"), "15.70796327");
});

test("text that is only a number, a constant, or a unit gets no card", () => {
  for (const text of ["10", "2024", "pi", "cup", "in", "best pizza 2025", "iphone 15 pro", "covid-19", "f-150", "100 g protein"]) {
    assert.equal(calculator().calculate(text), null, text);
  }
});

test("the sum shows as mathjs read it, with its units in words", () => {
  const result = calculator().calculate("20 kg * 10 m/s/s / 5W");
  assert.ok(result);
  assert.equal(words(result.sum), "20 kg × 10 m ÷ s ÷ s ÷ 5 W");
  assert.deepEqual(result.sum.map(part => part.kind).slice(0, 3), ["number", "unit", "operator"]);
  assert.equal(result.sumName, "kilograms, meters, seconds, watts");
  assert.equal(words(result.answer), "40 s/m");
  assert.equal(result.answerName, "Seconds per meter");
  assert.equal(result.copy, "40 s/m");
});

test("named units stay, and energy and charge answer in watt-hours and amp-hours", () => {
  assert.equal(answer("100 W * 3h"), "300 Wh");
  assert.equal(answer("2 A * 3 h"), "6 Ah");
  assert.equal(answer("2 kW * 30 min"), "1 kWh");
  assert.equal(answer("500 mA * 90 min"), "750 mAh");
  assert.equal(answer("500 milliAmp * 90 min"), "750 mAh");
  assert.equal(calculator().calculate("2 kW * 30 min")?.answerName, "Kilowatt-hour");
});

test("units run together read as their product, left to right", () => {
  const result = calculator().calculate("200 N / 5 Wm/s");
  assert.equal(result && words(result.sum), "200 N ÷ 5 W m ÷ s");
  assert.equal(result && words(result.answer), "40 m⁻²");
  assert.equal(answer("200 N / (5 Wm/s)"), "40 s²/m²");
});

test("min is minutes, and a cup is 250 ml", () => {
  assert.equal(answer("90 min to h"), "1.5 h");
  assert.equal(answer("min(3, 4) + 1"), "4");
  assert.equal(answer("1/2 cup to tsp"), "25 tsp");
});

test("a half-typed sum answers for what is complete", () => {
  const result = calculator().calculate("10m *");
  assert.ok(result);
  assert.equal(words(result.answer), "10 m");
  assert.deepEqual(result.sum.at(-1), { text: " ×", kind: "aside" });
});

test("a bare amount converts to the other measurement system", () => {
  assert.equal(answer("5 km"), "3.11 mi");
  assert.equal(calculator().calculate("5 km")?.answerName, "Miles");
  assert.equal(calculator().calculate("5 km")?.sumName, "kilometers");
  assert.equal(answer("10 kg"), "22 lb");
  assert.equal(answer("3 ft"), "91.4 cm");
  assert.equal(answer("20 degC"), "68 °F");
  assert.equal(answer("5 W"), undefined);
});

test("a whole number shows every digit, and a unit spelled as a word takes its plural", () => {
  assert.equal(answer("1 week to minutes"), "10,080 min");
  assert.equal(answer("66 days to hours"), "1,584 h");
  assert.equal(answer("12345678901 * 1"), "12,345,678,901");
  assert.equal(answer("66 days to weeks"), "9.43 weeks");
  assert.equal(answer("7 days to weeks"), "1 week");
  assert.equal(answer("2 lb to oz"), "32 oz");
});

test("a word that is not a unit shows no card, and units that do not fit show why", () => {
  assert.equal(calculator().calculate("1/2 cup flour"), null);
  assert.equal(calculator().calculate("100 usd to xyz"), null);
  assert.equal(calculator().calculate("10 kg + 5 m")?.why, "Cannot add a mass to a length");
});

test("currencies add up at the day's rates", () => {
  const result = calculator().calculate("10 HKD + 10 EUR to USD");
  assert.ok(result);
  assert.equal(words(result.sum), "10 HKD + 10 EUR");
  assert.equal(result.sumName, "Hong Kong dollars, euros");
  assert.equal(result.copy, "$12.09");
  assert.equal(result.answerName, "US dollars");
  assert.equal(result.note, "HK$10 = $1.29 · €10 = $10.80 · ECB rates of Oct 15, 2026");
});

test("a bare currency amount converts to the default currency", () => {
  assert.equal(calculator("HKD").calculate("100 usd")?.copy, "HK$777.78");
  assert.equal(calculator("HKD").calculate("100 hkd")?.copy, "$12.86");
  assert.equal(calculator("HKD").calculate("$100 to eur")?.copy, "€92.59");
  assert.equal(calculator("KRW").calculate("€20 + £5")?.copy, "₩37,529.41");
});

test("unit names spelled out ignore case, and symbols keep it", () => {
  assert.equal(answer("2 Kilometers + 500 m"), "2.5 km");
  assert.equal(answer("1 MA * 1 h"), "1 MAh");
});

test("shorthand amounts read as thousands, millions, billions, and trillions", () => {
  const result = calculator().calculate("180k usd to hkd");
  assert.ok(result);
  assert.equal(words(result.sum), "180,000 USD");
  assert.equal(result.copy, "HK$1,400,000");
  assert.equal(answer("$5K to usd"), "$5,000");
  assert.equal(answer("180k$ to usd"), "$180,000");
  assert.equal(answer("€1.5m to eur"), "€1,500,000");
  assert.equal(answer("5m usd to usd"), "$5,000,000");
  assert.equal(answer("HK$2bn to hkd"), "HK$2,000,000,000");
  assert.equal(answer("2B hkd to hkd"), "HK$2,000,000,000");
  assert.equal(answer("3tn usd to usd"), "$3,000,000,000,000");
  assert.equal(answer("1.5 Million usd to usd"), "$1,500,000");
});

test("plain numbers take only the shorthand that is not a unit", () => {
  const result = calculator().calculate("5k + 3k");
  assert.equal(result && words(result.sum), "5,000 + 3,000");
  assert.equal(result && words(result.answer), "8,000");
  assert.equal(answer("1.5M * 2"), "3,000,000");
  assert.equal(answer("2bn / 1000"), "2,000,000");
  assert.equal(answer("1tn / 1bn"), "1,000");
  assert.equal(answer("3 thousand + 200"), "3,200");
  assert.equal(answer("max(1,5k)"), "5,000");
  assert.equal(answer("5m"), "16.4 ft");
  assert.equal(answer("2b + 2b"), "4 b");
  assert.equal(answer("2B + 2B"), "4 B");
  assert.equal(answer("3t + 1t"), "4 t");
  assert.equal(answer("2T + 1T"), "3 T");
  assert.equal(answer("5K + 1K"), "6 K");
  for (const text of ["5k", "180 k usd", "10k steps", "5k run"]) {
    assert.equal(calculator().calculate(text), null, text);
  }
});
