import assert from "node:assert/strict";
import test from "node:test";
import * as mathjs from "mathjs";
import { Calculator, type Calculation, type MathJsLibrary } from "../browser/resources/uw_command_bar/calculator.ts";
import { TimeMath } from "../browser/resources/uw_command_bar/time.ts";

// 6:24:37 PM on Tuesday, Oct 20, 2026, in Hong Kong.
const NOW = Date.UTC(2026, 9, 20, 10, 24, 37);

function time(hour24 = false, now = NOW): TimeMath {
  const time = new TimeMath(new Calculator(mathjs as unknown as MathJsLibrary, "en-US"), "en-US");
  time.setClock("Asia/Hong_Kong", hour24, () => now);
  return time;
}

function words(ink: Calculation["sum"]): string {
  return ink.map(part => part.text).join("");
}

// The sum, the pill under it, the answer, and the pill under the answer.
function card(text: string, hour24 = false): string | null | undefined {
  const result = time(hour24).calculate(text);
  if (!result) {
    return result;
  }
  return [words(result.sum), result.sumName, result.why || words(result.answer), result.why ? "No answer" : result.answerName].join(" | ");
}

test("the plan's examples, typed in Hong Kong at 6:24 PM on Tuesday, Oct 20, 2026", () => {
  assert.equal(card("now +"), "now + | Hong Kong | 6:24 PM | Tuesday, Oct 20");
  assert.equal(card("today"), "today | Hong Kong | Tuesday, Oct 20, 2026 | Day 293 of 2026");
  assert.equal(card("3pm"), "3 PM | Hong Kong | 3:00 PM | Today, 3 h 24 min ago");
  assert.equal(card("dec 25"), "Dec 25 | Date | Friday, Dec 25, 2026 | In 66 days");
  assert.equal(card("dec 25 - today"), "Dec 25 − today | Expression | 66 days | 2 months 5 days");
  assert.equal(card("5:30pm - 9:15am"), "5:30 PM − 9:15 AM | Expression | 8 h 15 min | Duration");
  assert.equal(card("today + 3 weeks"), "today + 3 weeks | Hong Kong | Tuesday, Nov 10, 2026 | In 3 weeks");
  assert.equal(card("now + 100 minutes to UTC"), "now + 100 minutes | Today, 6:24 PM | 12:04 PM | Today, UTC");
  assert.equal(card("3pm London"), "3 PM London | London time | 10:00 PM | Today, Hong Kong");
  assert.equal(card("1760000000 to date"), "1760000000 | Unix time | Oct 9, 2025, 4:53:20 PM | Thursday");
});

test("an abbreviation stands for one place and follows its clock", () => {
  assert.equal(card("3pm PST"), "3 PM PST | PST · Los Angeles | 6:00 AM | Tomorrow, Hong Kong");
  assert.equal(card("3pm PDT"), "3 PM PDT | PDT · Los Angeles | 6:00 AM | Tomorrow, Hong Kong");
  assert.equal(card("3pm IST"), "3 PM IST | IST · India | 5:30 PM | Today, Hong Kong");
  assert.equal(card("3pm utc+8"), "3 PM UTC+8 | UTC+8 | 3:00 PM | Today, Hong Kong");
});

test("places are cities, zone names, countries with one zone, and big cities", () => {
  assert.equal(time().calculate("9am san francisco")?.copy, "12:00 AM");
  assert.equal(time().calculate("9am new york")?.copy, "9:00 PM");
  assert.equal(time().calculate("9am Asia/Tokyo")?.copy, "8:00 AM");
  assert.equal(time().calculate("9am japan")?.copy, "8:00 AM");
  assert.equal(time().calculate("9am são paulo")?.copy, "8:00 PM");
  assert.equal(time().calculate("now in tokyo")?.copy, "7:24 PM");
});

test("dates have a month name or are written year first, and slashes stay math", () => {
  assert.equal(card("25 dec"), "Dec 25 | Date | Friday, Dec 25, 2026 | In 66 days");
  assert.equal(card("december 25 2026"), "Dec 25, 2026 | Date | Friday, Dec 25, 2026 | In 66 days");
  assert.equal(card("2026-12-25"), "Dec 25, 2026 | Date | Friday, Dec 25, 2026 | In 66 days");
  assert.equal(time().calculate("jan 5")?.copy, "Tuesday, Jan 5, 2027");
  assert.equal(time().calculate("jan 31 2027 + 1 month")?.copy, "Sunday, Feb 28, 2027");
  assert.equal(time().calculate("3/4"), undefined);
  assert.equal(time().calculate("12/25"), undefined);
});

test("weekdays, next and last, and in before a length of time", () => {
  assert.equal(card("next friday"), "next Friday | Date | Friday, Oct 30, 2026 | In 10 days");
  assert.equal(time().calculate("last friday")?.copy, "Friday, Oct 16, 2026");
  assert.equal(time().calculate("friday - today")?.copy, "3 days");
  assert.equal(time().calculate("friday 3pm london")?.copy, "Oct 23, 2026, 10:00 PM");
  assert.equal(time().calculate("dec 25 3pm london")?.copy, "Dec 25, 2026, 11:00 PM");
  assert.equal(card("today in 3 days"), "today in 3 days | Hong Kong | Friday, Oct 23, 2026 | In 3 days");
  assert.equal(time().calculate("now in 2 hours")?.copy, "8:24 PM");
  // Friday, Oct 23: friday is today, and next friday a week later.
  const friday = time(false, Date.UTC(2026, 9, 23, 4));
  assert.equal(friday.calculate("friday - today")?.copy, "0 days");
  assert.equal(friday.calculate("next friday")?.copy, "Friday, Oct 30, 2026");
});

test("timestamps both ways, in seconds and without commas", () => {
  assert.equal(card("1760000000000 to date"), "1760000000000 | Unix time · ms | Oct 9, 2025, 4:53:20 PM | Thursday");
  assert.equal(card("now to unix"), "now | Today, 6:24 PM | 1792491840 | Unix time");
  assert.equal(card("dec 25 to unix"), "Dec 25 | Midnight, Hong Kong | 1798128000 | Unix time");
  assert.equal(time().calculate("1760000000 to date")?.size, "datetime");
});

test("a gap keeps its sign, is worked out as written, and converts", () => {
  assert.equal(time().calculate("9:15am - 5:30pm")?.copy, "−8 h 15 min");
  assert.equal(time().calculate("2am - 10pm")?.copy, "−20 h");
  assert.equal(time().calculate("tomorrow 2am - 10pm")?.copy, "4 h");
  assert.equal(time().calculate("dec 25 - today to hours")?.copy, "1,584 h");
  assert.equal(time().calculate("dec 25 - today to weeks")?.copy, "9.43 weeks");
  assert.equal(time().calculate("5:30pm - 9:15am in minutes")?.copy, "495 min");
});

test("text a card is not shown for", () => {
  for (const text of ["now", "friday", "dinner tomorrow", "meeting at 3pm", "now in Narnia", "today + 3 bananas", "in 3 days", "3 days ago"]) {
    assert.equal(time().calculate(text) ?? null, null, text);
  }
  for (const text of ["tokyo", "tokyo hotels", "1760000000"]) {
    assert.equal(time().calculate(text), undefined, text);
  }
});

test("text read in full that does not work out says why", () => {
  assert.equal(card("feb 30"), "Feb 30 | Date | Feb 30 is not a date | No answer");
  assert.equal(card("13:75"), "13:75 | Hong Kong | 13:75 is not a time | No answer");
  assert.equal(card("today + 3 kg"), "today + 3 kg | Hong Kong | Cannot add a date to a mass | No answer");
});

test("times follow the Mac's 24-hour setting", () => {
  assert.equal(card("3pm", true), "15:00 | Hong Kong | 15:00 | Today, 3 h 24 min ago");
});

test("month ends and daylight saving follow Temporal", () => {
  assert.equal(time().calculate("mar 14 2027 2:30am new york to new york")?.copy, "Mar 14, 2027, 3:30 AM");
});

test("a card built on now moves on with the minute", () => {
  const result = time().calculate("now +");
  assert.equal(result?.live, true);
  assert.equal(time(false, NOW + 60_000).calculate("now +")?.copy, "6:25 PM");
});
