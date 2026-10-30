// A watched page's first tracked value over its runs: the words under its
// latest value, and the Past 30 days trend beside it. See the frames
// "Scheduled page trends: the page (Fluid)" and "Scheduled page trends: every
// state (Fluid)" on the canvas page "Agent chats", the canvas guide
// uw-scheduled-page-and-inbox, and docs/product.md, "Scheduled chats".
import { useId, useState, type ReactElement } from "react";
import { TrendingDown, TrendingUp } from "lucide-react";
import { cn } from "@/lib/utils";
import { RunEnd, type Step, type TrackedValue } from "../chat-api";
import { didNotFinish, nearest, numberText, thresholdOf } from "../chart";

const DAYS = 30;
const WIDTH = 136;
const HEIGHT = 30;
const PAD = 3;

export function daysFromToday(ms: number): number {
  const day = new Date(ms).setHours(0, 0, 0, 0);
  return Math.round((day - new Date().setHours(0, 0, 0, 0)) / 86_400_000);
}

export function dateText(ms: number): string {
  return new Date(ms).toLocaleDateString([], {
    month: "short",
    day: "numeric",
  });
}

// "today", "yesterday", or "Oct 28".
function dayWord(ms: number): string {
  const days = daysFromToday(ms);
  return days === 0 ? "today" : days === -1 ? "yesterday" : dateText(ms);
}

function shownValue(value: TrackedValue): string {
  if (value.number !== null) {
    return numberText(value.number, value.unit);
  }
  if (value.yes !== null) {
    return value.yes ? "Yes" : "No";
  }
  return value.text ?? "";
}

function kindOf(value: TrackedValue): "number" | "yes" | "text" {
  return value.number !== null ? "number" : value.yes !== null ? "yes" : "text";
}

// Whether `b` carries on `a`'s line: the same kind of value in the same unit.
function sameSeries(a: TrackedValue, b: TrackedValue): boolean {
  return kindOf(a) === kindOf(b) && a.unit === b.unit;
}

function sameValue(a: TrackedValue, b: TrackedValue): boolean {
  return (
    sameSeries(a, b) &&
    a.number === b.number &&
    a.yes === b.yes &&
    a.text === b.text
  );
}

// A run of the value, or of a run that didn't finish (`value` null).
interface Point {
  time: number;
  value: TrackedValue | null;
}

// The runs of the first value a watch's runs named, oldest first.
function pointsOf(runs: Step[]): Point[] {
  const name = runs.find(
    (line) =>
      line.run?.end === RunEnd.kReported &&
      !didNotFinish(line) &&
      line.run.values.length,
  )?.run!.values[0]!.name;
  if (!name) {
    return [];
  }
  return runs.flatMap((line): Point[] => {
    const run = line.run;
    if (!run || run.end === RunEnd.kStopped) {
      return [];
    }
    if (didNotFinish(line)) {
      return [{ time: run.time, value: null }];
    }
    const value = run.values.find((each) => each.name === name);
    return value ? [{ time: run.time, value }] : [];
  });
}

export interface Latest {
  value: string;
  // What it was before its last change, and the day it changed; null for a
  // value that never changed, which says since when instead.
  was: { value: string; until: string } | null;
  since: string;
  direction: "up" | "down" | null;
  // Whether it moved the way a Watch for chip hopes.
  good: boolean;
}

export function latestOf(runs: Step[], watchFor: string): Latest | null {
  const points = pointsOf(runs).flatMap((point) =>
    point.value ? [{ time: point.time, value: point.value }] : [],
  );
  const last = points.at(-1);
  if (!last) {
    return null;
  }
  let start = points.length - 1;
  while (start > 0 && sameValue(points[start - 1]!.value, last.value)) {
    start--;
  }
  const changed = points[start]!.time;
  const before = start > 0 ? points[start - 1]!.value : null;
  const now = last.value;
  if (!before || !sameSeries(before, now)) {
    return {
      value: shownValue(now),
      was: null,
      since: `Since ${dayWord(changed)}`,
      direction: null,
      good: false,
    };
  }
  const direction =
    now.number !== null && before.number !== null
      ? now.number < before.number
        ? "down"
        : "up"
      : null;
  return {
    value: shownValue(now),
    was: { value: shownValue(before), until: dayWord(changed) },
    since: "",
    direction,
    good:
      (direction === "down" && watchFor.startsWith("Price drops")) ||
      (before.yes === false &&
        now.yes === true &&
        watchFor.startsWith("Back in stock")),
  };
}

// The words under the latest value: "was $999 until yesterday", or "Since
// Oct 29".
export function Change({ latest }: { latest: Latest }) {
  if (!latest.was) {
    return <>{latest.since}</>;
  }
  const Arrow =
    latest.direction === "down"
      ? TrendingDown
      : latest.direction === "up"
        ? TrendingUp
        : null;
  return (
    <span
      className={cn(
        "flex min-w-0 items-center gap-1.5",
        latest.good && "text-success",
      )}
    >
      {Arrow && <Arrow aria-hidden />}
      <span className="flex min-w-0 gap-[0.28em]">
        <span className="truncate">was {latest.was.value}{" "}</span>
        <span className="shrink-0">until {latest.was.until}</span>
      </span>
    </span>
  );
}

const at = (n: number) => n.toFixed(1);

// The Past 30 days cell: a line for a number, a strip for yes or no, and a
// dot for each change of a text, from a watch's second run. Time runs the
// same on every row, so a young watch draws only at the right.
export function Trend({
  runs,
  watchFor,
  paused,
  good,
}: {
  runs: Step[];
  watchFor: string;
  paused: boolean;
  good: boolean;
}) {
  const id = useId();
  const [hovered, setHovered] = useState<number | null>(null);
  const now = Date.now();
  const from = now - DAYS * 86_400_000;
  const all = pointsOf(runs);
  const last = all.findLast((point) => point.value)?.value;
  // A change of unit starts the line again.
  const restart = last
    ? all.findLastIndex((point) => point.value && !sameSeries(point.value, last)) + 1
    : 0;
  const points = all.slice(restart).filter((point) => point.time >= from);
  const values = points.flatMap((point) => (point.value ? [point.value] : []));
  if (!last || values.length < 2) {
    return <div />;
  }
  const kind = kindOf(last);
  const x = (ms: number) =>
    PAD + ((Math.min(ms, now) - from) / (now - from)) * (WIDTH - 2 * PAD);
  const right = WIDTH - PAD;
  const mid = HEIGHT / 2;

  let low = 0;
  let high = 0;
  const alert = kind === "number" ? thresholdOf(watchFor) : null;
  if (kind === "number") {
    const numbers = values.map((value) => value.number!);
    low = Math.min(...numbers);
    high = Math.max(...numbers);
    const reach = high - low || Math.abs(high) * 0.1;
    if (alert !== null && alert >= low - reach && alert <= high + reach) {
      low = Math.min(low, alert);
      high = Math.max(high, alert);
    }
  }
  const showAlert = alert !== null && alert >= low && alert <= high;
  const y = (value: number) =>
    high === low ? mid : PAD + ((high - value) / (high - low)) * (HEIGHT - 2 * PAD);

  const drawn: ReactElement[] = [];
  const lastRun = x(points.at(-1)!.time);
  if (kind === "number") {
    let solid = "";
    let dotted = "";
    let area = "";
    let previous: [number, number] | null = null;
    let gap = false;
    for (const point of points) {
      if (!point.value) {
        gap = previous !== null;
        continue;
      }
      const here: [number, number] = [x(point.time), y(point.value.number!)];
      const to = `${at(here[0])} ${at(here[1])}`;
      area += area ? `L${to}` : `M${at(here[0])} ${HEIGHT}L${to}`;
      if (!previous || gap) {
        if (previous) {
          dotted += `M${at(previous[0])} ${at(previous[1])}L${to}`;
        }
        solid += `M${to}`;
      } else {
        solid += `L${to}`;
      }
      previous = here;
      gap = false;
    }
    const end = previous!;
    if (paused) {
      dotted += `M${at(end[0])} ${at(end[1])}H${right}`;
    }
    drawn.push(
      <linearGradient key="fill" id={`${id}fill`} x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stopColor="currentColor" stopOpacity={0.28} />
        <stop offset="1" stopColor="currentColor" stopOpacity={0} />
      </linearGradient>,
    );
    if (showAlert) {
      drawn.push(
        <path
          key="alert"
          data-alert
          d={`M${PAD} ${at(y(alert))}H${right}`}
          className="stroke-foreground/30"
          strokeDasharray="3 3"
        />,
      );
    }
    drawn.push(
      <path key="area" d={`${area}L${at(end[0])} ${HEIGHT}Z`} fill={`url(#${id}fill)`} />,
      <path
        key="line"
        d={solid}
        fill="none"
        stroke="currentColor"
        strokeWidth={1.5}
        strokeLinejoin="round"
        strokeLinecap="round"
      />,
    );
    if (dotted) {
      drawn.push(
        <path
          key="gap"
          data-gap
          d={dotted}
          fill="none"
          stroke="currentColor"
          strokeWidth={1.5}
          strokeDasharray="1.5 3.5"
          strokeLinecap="round"
        />,
      );
    }
    drawn.push(
      <circle
        key="end"
        cx={end[0]}
        cy={end[1]}
        r={2.5}
        fill="currentColor"
        className="stroke-background"
        strokeWidth={1.5}
      />,
    );
  } else if (kind === "yes") {
    drawn.push(
      <pattern
        key="hatch"
        id={`${id}hatch`}
        width="4"
        height="4"
        patternUnits="userSpaceOnUse"
        patternTransform="rotate(45)"
      >
        <rect width="4" height="4" className="fill-foreground/5" />
        <path d="M0 0V4" className="stroke-foreground/35" strokeWidth={1.5} />
      </pattern>,
    );
    // Each run's span lasts until the next run; a run's own kind of span
    // joins the one before it.
    const spans: { kind: "yes" | "no" | "failed"; from: number; to: number }[] = [];
    points.forEach((point, index) => {
      const span = !point.value ? "failed" : point.value.yes ? "yes" : "no";
      const start = x(point.time);
      const next = points[index + 1];
      const end = next ? x(next.time) : paused ? start + 4 : right;
      const before = spans.at(-1);
      if (before?.kind === span) {
        before.to = end;
      } else {
        spans.push({ kind: span, from: start, to: end });
      }
    });
    spans.forEach((span, index) => {
      drawn.push(
        <rect
          key={index}
          data-span={span.kind}
          x={at(span.from)}
          y={11}
          width={at(Math.max(2, span.to - span.from - (index < spans.length - 1 ? 1 : 0)))}
          height={8}
          rx={2}
          className={
            span.kind === "yes"
              ? "fill-success"
              : span.kind === "no"
                ? "fill-foreground/15"
                : undefined
          }
          fill={span.kind === "failed" ? `url(#${id}hatch)` : undefined}
        />,
      );
    });
    if (paused) {
      drawn.push(
        <path
          key="paused"
          data-gap
          d={`M${at(spans.at(-1)!.to + 3)} ${mid}H${right}`}
          className="stroke-muted-foreground/80"
          strokeWidth={1.5}
          strokeDasharray="1.5 3.5"
          strokeLinecap="round"
        />,
      );
    }
  } else {
    const changes: number[] = [];
    let before: TrackedValue | null = null;
    for (const point of all.slice(restart)) {
      if (!point.value) {
        continue;
      }
      if (before && !sameValue(before, point.value) && point.time >= from) {
        changes.push(x(point.time));
      }
      before = point.value;
    }
    const start = x(points[0]!.time);
    drawn.push(
      <path
        key="base"
        d={`M${at(start)} ${mid}H${at(paused ? lastRun : right)}`}
        className="stroke-foreground/15"
      />,
    );
    if (paused) {
      drawn.push(
        <path
          key="paused"
          data-gap
          d={`M${at(lastRun)} ${mid}H${right}`}
          className="stroke-muted-foreground/80"
          strokeWidth={1.5}
          strokeDasharray="1.5 3.5"
          strokeLinecap="round"
        />,
      );
    }
    changes.forEach((change, index) => {
      const newest = index === changes.length - 1;
      drawn.push(
        <circle
          key={index}
          data-change
          cx={change}
          cy={mid}
          r={newest ? 3.5 : 3}
          className={newest ? "fill-foreground" : "fill-muted-foreground/60"}
        />,
      );
    });
  }

  const point = hovered !== null ? points[hovered] : undefined;
  if (point && kind === "number") {
    const there = x(point.time);
    drawn.push(
      <path
        key="scrub"
        d={`M${at(there)} 0V${HEIGHT}`}
        className="stroke-foreground/30"
        strokeDasharray="2 2"
      />,
    );
    if (point.value) {
      drawn.push(
        <circle
          key="hovered"
          cx={there}
          cy={y(point.value.number!)}
          r={2.5}
          className="fill-foreground stroke-background"
          strokeWidth={1.5}
        />,
      );
    }
  }
  const unit = last.unit;
  return (
    <div
      data-trend={kind}
      role="img"
      aria-label={`Past 30 days, from ${shownValue(values[0]!)} to ${shownValue(last)}`}
      className="relative h-[30px]"
      onPointerMove={(event) => {
        const box = event.currentTarget.getBoundingClientRect();
        setHovered(
          nearest(
            points.map((each) => x(each.time)),
            event.clientX - box.left,
          ),
        );
      }}
      onPointerLeave={() => setHovered(null)}
    >
      <svg
        width={WIDTH}
        height={HEIGHT}
        className={cn(
          "overflow-visible",
          good ? "text-success" : "text-muted-foreground/80",
        )}
      >
        {drawn}
      </svg>
      {point && (
        <div
          role="status"
          className="pointer-events-none absolute bottom-[calc(100%+8px)] z-10 -translate-x-1/2 rounded-md bg-foreground px-2 py-1 text-[11.5px] leading-snug font-medium whitespace-nowrap text-background shadow-lg"
          style={{ left: x(point.time) }}
        >
          {dateText(point.time)} ·{" "}
          {point.value ? shownValue(point.value) : "Didn't finish"}
          {alert !== null && (
            <div className="font-normal opacity-70">
              Alerts below {numberText(alert, unit)}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
