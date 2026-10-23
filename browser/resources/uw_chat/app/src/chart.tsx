// Charts of a scheduled chat's tracked values: a number is a line and a yes
// or no value a strip of spans; text values are not charted. See the frames
// "Scheduled chat: every state" and "Watch this page: every state" on the
// canvas page "Agent chats", and docs/product.md, "Scheduled chats".
import {
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type KeyboardEvent,
} from "react";
import { cn } from "@/lib/utils";
import { RunEnd, type Step, type TrackedValue } from "./chat-api";

// A value's name as a person reads it: "in_stock" reads "In stock".
export function valueLabel(name: string): string {
  const words = name.replaceAll(/[_-]+/g, " ").trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
}

// A number in its unit: "$549" for a currency code, else "12.5 kg".
export function numberText(number: number, unit: string): string {
  if (/^[A-Z]{3}$/.test(unit)) {
    try {
      return new Intl.NumberFormat(undefined, {
        style: "currency",
        currency: unit,
        maximumFractionDigits: Number.isInteger(number) ? 0 : 2,
      }).format(number);
    } catch {
      // Not a currency after all.
    }
  }
  const text = number.toLocaleString();
  return unit ? `${text} ${unit}` : text;
}

// A value as a person reads it: "$549", "In stock: yes", or "Size: B".
export function valueText(value: TrackedValue): string {
  if (value.number !== null) {
    return numberText(value.number, value.unit);
  }
  if (value.yes !== null) {
    return `${valueLabel(value.name)}: ${value.yes ? "yes" : "no"}`;
  }
  return `${valueLabel(value.name)}: ${value.text ?? ""}`;
}

// A run on a chart, with its value, or null for a run that didn't finish.
export interface Point {
  line: Step;
  value: number | boolean | null;
}

export interface Series {
  name: string;
  unit: string;
  kind: "number" | "yes";
  points: Point[];
}

function didNotFinish(line: Step): boolean {
  const run = line.run!;
  return (
    run.end === RunEnd.kFailed ||
    run.end === RunEnd.kUnreachable ||
    (run.end === RunEnd.kReported && run.counted)
  );
}

// The charted values of `runs`, oldest first, in the order runs first
// reported them. A run that didn't finish is a gap in each.
export function chartSeries(runs: Step[]): Series[] {
  const series = new Map<string, Series>();
  for (const line of runs) {
    for (const value of line.run?.values ?? []) {
      if (series.has(value.name) || didNotFinish(line)) {
        continue;
      }
      if (value.number !== null || value.yes !== null) {
        series.set(value.name, {
          name: value.name,
          unit: value.unit,
          kind: value.number !== null ? "number" : "yes",
          points: [],
        });
      }
    }
  }
  for (const each of series.values()) {
    for (const line of runs) {
      if (!line.run || line.run.end === RunEnd.kStopped) {
        continue;
      }
      if (didNotFinish(line)) {
        each.points.push({ line, value: null });
        continue;
      }
      const value = line.run.values.find((one) => one.name === each.name);
      const kept = each.kind === "number" ? value?.number : value?.yes;
      if (kept !== null && kept !== undefined) {
        each.points.push({ line, value: kept });
      }
    }
  }
  return [...series.values()];
}

// The price under which a watch's "Only below" chip tells the user, such as
// 500 for "Price drops below $500".
export function thresholdOf(watchFor: string): number | null {
  const below = /^Price drops below (.+)$/.exec(watchFor)?.[1];
  const number = below ? Number(below.replaceAll(/[^0-9.]/g, "")) : NaN;
  return Number.isFinite(number) && number > 0 ? number : null;
}

// Scrolls the chat to the line of the run at `time`, or to the folded line
// that holds it.
export function scrollToRun(time: number) {
  const exact = document.querySelector<HTMLElement>(
    `[data-run-time="${time}"]`,
  );
  const folded = [
    ...document.querySelectorAll<HTMLElement>("[data-run-from]"),
  ].find(
    (element) =>
      Number(element.dataset.runFrom) <= time &&
      time <= Number(element.dataset.runTo),
  );
  (exact ?? folded)?.scrollIntoView({ block: "center" });
}

function dayText(ms: number): string {
  const day = new Date(ms).setHours(0, 0, 0, 0);
  if (day === new Date().setHours(0, 0, 0, 0)) {
    return "Today";
  }
  return new Date(ms).toLocaleDateString([], { month: "short", day: "numeric" });
}

function clockText(ms: number): string {
  return new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
}

function pointText(series: Series, point: Point): string {
  const at = `${dayText(point.line.run!.time)} · ${clockText(point.line.run!.time)}`;
  if (point.value === null) {
    return `${at} · Didn't finish · ${point.line.text}`;
  }
  const value =
    typeof point.value === "number"
      ? numberText(point.value, series.unit)
      : point.value
        ? "Yes"
        : "No";
  return `${at} · ${point.line.text} · ${value}`;
}

function useWidth<T extends HTMLElement>() {
  const ref = useRef<T>(null);
  const [width, setWidth] = useState(0);
  useLayoutEffect(() => {
    const element = ref.current;
    if (!element) {
      return;
    }
    setWidth(element.clientWidth);
    const observer = new ResizeObserver(() => setWidth(element.clientWidth));
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  return [ref, width] as const;
}

// The pointer's run on a chart, which the tooltip reads and the arrow keys
// move.
function useHovered(count: number, onPick?: (point: number) => void) {
  const [hovered, setHovered] = useState<number | null>(null);
  useEffect(() => {
    if (hovered !== null && hovered >= count) {
      setHovered(null);
    }
  }, [count, hovered]);
  const onKeyDown = (event: KeyboardEvent) => {
    if (!count) {
      return;
    }
    if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
      event.preventDefault();
      const step = event.key === "ArrowLeft" ? -1 : 1;
      setHovered((at) =>
        Math.min(count - 1, Math.max(0, (at ?? count) + step)),
      );
    } else if (event.key === "Enter" && hovered !== null) {
      onPick?.(hovered);
    }
  };
  return { hovered, setHovered, onKeyDown };
}

// The run nearest `x` among `xs`.
function nearest(xs: number[], x: number): number {
  let best = 0;
  xs.forEach((each, at) => {
    if (Math.abs(each - x) < Math.abs(xs[best]! - x)) {
      best = at;
    }
  });
  return best;
}

function Tooltip({ x, width, text }: { x: number; width: number; text: string }) {
  const ref = useRef<HTMLDivElement>(null);
  const [own, setOwn] = useState(0);
  useLayoutEffect(() => setOwn(ref.current?.offsetWidth ?? 0), [text]);
  const left = Math.min(Math.max(0, x - own / 2), Math.max(0, width - own));
  return (
    <div
      ref={ref}
      role="status"
      className="chart-tooltip pointer-events-none absolute top-1 z-10 max-w-full truncate rounded-md bg-foreground px-2 py-1 text-xs whitespace-nowrap text-background"
      style={{ left }}
    >
      {text}
    </div>
  );
}

// The dates under a chart: its first run, the first that didn't finish when
// there is room, and its last.
function DateLabels({
  first,
  last,
  failure,
}: {
  first: number;
  last: number;
  failure?: { x: number; ms: number; width: number };
}) {
  const room = 90;
  return (
    <div className="relative mt-1 h-4 text-[11px] text-muted-foreground">
      <span className="absolute left-0">{dayText(first)}</span>
      {failure &&
        failure.x > room &&
        failure.x < failure.width - room && (
          <span
            className="absolute -translate-x-1/2 whitespace-nowrap"
            style={{ left: failure.x }}
          >
            {dayText(failure.ms)}, didn't finish
          </span>
        )}
      <span className="absolute right-0">{dayText(last)}</span>
    </div>
  );
}

// A number's runs as a line. A dashed line marks `threshold`, and a run that
// didn't finish leaves a gap, dotted across.
export function LineChart({
  series,
  height,
  threshold,
  onPick,
}: {
  series: Series;
  height: number;
  threshold?: number | null;
  onPick?: (line: Step) => void;
}) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const points = series.points;
  const { hovered, setHovered, onKeyDown } = useHovered(points.length, (at) =>
    onPick?.(points[at]!.line),
  );
  if (!points.length) {
    return null;
  }
  const times = points.map((point) => point.line.run!.time);
  const first = times[0]!;
  const last = Math.max(times.at(-1)!, first + 1);
  const pad = 6;
  const x = (ms: number) =>
    pad + ((ms - first) / (last - first)) * Math.max(0, width - 2 * pad);
  const numbers = points.flatMap((point) =>
    typeof point.value === "number" ? [point.value] : [],
  );
  const shown = threshold ? [...numbers, threshold] : numbers;
  let low = Math.min(...shown);
  let high = Math.max(...shown);
  if (low === high) {
    low -= 1;
    high += 1;
  }
  const margin = (high - low) * 0.12;
  low -= margin;
  high += margin;
  const y = (value: number) => ((high - value) / (high - low)) * height;

  const segments: { from: [number, number]; to: [number, number]; gap: boolean }[] = [];
  let previous: [number, number] | null = null;
  let gap = false;
  for (const point of points) {
    if (typeof point.value !== "number") {
      gap = previous !== null;
      continue;
    }
    const at: [number, number] = [x(point.line.run!.time), y(point.value)];
    if (previous) {
      segments.push({ from: previous, to: at, gap });
    }
    previous = at;
    gap = false;
  }
  const lastNumber = [...points]
    .reverse()
    .find((point) => typeof point.value === "number");
  const failure = points.find((point) => point.value === null);
  const hoveredPoint = hovered !== null ? points[hovered] : undefined;
  return (
    <div className="line-chart" data-value={series.name}>
      <div
        ref={ref}
        role="img"
        tabIndex={0}
        aria-label={`${valueLabel(series.name)} over time. Use the arrow keys to read each run.`}
        className="relative rounded-md outline-none focus-visible:ring-2 focus-visible:ring-ring"
        style={{ height }}
        onPointerMove={(event) => {
          const box = event.currentTarget.getBoundingClientRect();
          setHovered(nearest(times.map(x), event.clientX - box.left));
        }}
        onPointerLeave={() => setHovered(null)}
        onClick={() => hoveredPoint && onPick?.(hoveredPoint.line)}
        onKeyDown={onKeyDown}
      >
        {width > 0 && (
          <svg width={width} height={height} className="overflow-visible">
            {threshold && (
              <>
                <line
                  x1={0}
                  x2={width}
                  y1={y(threshold)}
                  y2={y(threshold)}
                  className="stroke-muted-foreground/70"
                  strokeWidth={1}
                  strokeDasharray="4 4"
                />
                <text
                  x={2}
                  y={y(threshold) - 4}
                  className="fill-muted-foreground text-[10px]"
                >
                  {numberText(threshold, series.unit)}
                </text>
              </>
            )}
            {hoveredPoint && (
              <line
                x1={x(hoveredPoint.line.run!.time)}
                x2={x(hoveredPoint.line.run!.time)}
                y1={0}
                y2={height}
                className="stroke-border"
                strokeWidth={1}
              />
            )}
            {segments.map((segment, at) => (
              <line
                key={at}
                x1={segment.from[0]}
                y1={segment.from[1]}
                x2={segment.to[0]}
                y2={segment.to[1]}
                className="stroke-primary"
                strokeWidth={2}
                strokeLinecap="round"
                strokeDasharray={segment.gap ? "2 4" : undefined}
                opacity={segment.gap ? 0.6 : 1}
              />
            ))}
            {[hoveredPoint, lastNumber].map(
              (point, at) =>
                point &&
                typeof point.value === "number" && (
                  <circle
                    key={at}
                    cx={x(point.line.run!.time)}
                    cy={y(point.value)}
                    r={4}
                    className="fill-primary stroke-background"
                    strokeWidth={2}
                  />
                ),
            )}
          </svg>
        )}
        {hoveredPoint && width > 0 && (
          <Tooltip
            x={x(hoveredPoint.line.run!.time)}
            width={width}
            text={pointText(series, hoveredPoint)}
          />
        )}
      </div>
      <DateLabels
        first={first}
        last={times.at(-1)!}
        failure={
          failure && {
            x: x(failure.line.run!.time),
            ms: failure.line.run!.time,
            width,
          }
        }
      />
    </div>
  );
}

const HATCH =
  "repeating-linear-gradient(135deg, var(--border) 0 3px, transparent 3px 6px)";

// A yes or no value's runs as a strip: each run's span lasts until the next.
export function YesNoStrip({
  series,
  onPick,
}: {
  series: Series;
  onPick?: (line: Step) => void;
}) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const points = series.points;
  const { hovered, setHovered, onKeyDown } = useHovered(points.length, (at) =>
    onPick?.(points[at]!.line),
  );
  if (!points.length) {
    return null;
  }
  const times = points.map((point) => point.line.run!.time);
  const first = times[0]!;
  const end = Math.max(Date.now(), times.at(-1)! + 1);
  const x = (ms: number) => ((ms - first) / (end - first)) * width;
  const hoveredPoint = hovered !== null ? points[hovered] : undefined;
  return (
    <div className="yes-no-strip" data-value={series.name}>
      <div
        ref={ref}
        role="img"
        tabIndex={0}
        aria-label={`${valueLabel(series.name)} over time. Use the arrow keys to read each run.`}
        className="relative h-[22px] rounded-md outline-none focus-visible:ring-2 focus-visible:ring-ring"
        onPointerLeave={() => setHovered(null)}
        onKeyDown={onKeyDown}
      >
        {width > 0 &&
          points.map((point, at) => {
            const left = x(times[at]!);
            const right = at + 1 < points.length ? x(times[at + 1]!) : width;
            const span = Math.max(1, right - left - 2);
            const label =
              point.value === null ? "" : point.value ? "Yes" : "No";
            return (
              <div
                key={at}
                className={cn(
                  "absolute inset-y-0 flex items-center justify-center overflow-hidden rounded-[3px] text-[11px] font-medium",
                  point.value === true && "bg-success text-background",
                  point.value === false &&
                    "bg-muted-foreground/25 text-foreground",
                  hovered === at && "ring-2 ring-foreground/30",
                )}
                style={{
                  left,
                  width: span,
                  background: point.value === null ? HATCH : undefined,
                }}
                onPointerEnter={() => setHovered(at)}
                onClick={() => onPick?.(point.line)}
              >
                {span > 32 && label}
              </div>
            );
          })}
        {hoveredPoint && width > 0 && (
          <div className="absolute inset-x-0 -top-8">
            <Tooltip
              x={x(hoveredPoint.line.run!.time)}
              width={width}
              text={pointText(series, hoveredPoint)}
            />
          </div>
        )}
      </div>
      <DateLabels first={first} last={times.at(-1)!} />
      <div className="mt-1 flex gap-3 text-[11px] text-muted-foreground">
        <span className="flex items-center gap-1">
          <span className="size-2 rounded-[2px] bg-success" />
          Yes
        </span>
        <span className="flex items-center gap-1">
          <span className="size-2 rounded-[2px] bg-muted-foreground/25" />
          No
        </span>
        <span className="flex items-center gap-1">
          <span className="size-2 rounded-[2px]" style={{ background: HATCH }} />
          Didn't finish
        </span>
      </div>
    </div>
  );
}

// One value's chart, as its kind draws it.
export function ValueChart({
  series,
  height,
  threshold,
  onPick,
}: {
  series: Series;
  height: number;
  threshold?: number | null;
  onPick?: (line: Step) => void;
}) {
  return series.kind === "number" ? (
    <LineChart
      series={series}
      height={height}
      threshold={threshold}
      onPick={onPick}
    />
  ) : (
    <YesNoStrip series={series} onPick={onPick} />
  );
}

// The site of a watched page, such as "fully.com", or empty.
function siteOf(page: string): string {
  try {
    return page ? new URL(page).hostname.replace(/^www\./, "") : "";
  } catch {
    return "";
  }
}

const RANGES: { label: string; days: number | null }[] = [
  { label: "7 days", days: 7 },
  { label: "30 days", days: 30 },
  { label: "All", days: null },
];

// The chart the schedule strip's Chart button opens above the conversation:
// each charted value of the chat's runs, over 7 days, 30 days, or all of them.
// A click on a run scrolls to its line.
export function ScheduleChart({
  runs,
  page,
  watchFor,
}: {
  runs: Step[];
  page: string;
  watchFor: string;
}) {
  const [days, setDays] = useState<number | null>(30);
  const since = days === null ? 0 : Date.now() - days * 86_400_000;
  const series = chartSeries(runs.filter((line) => line.run!.time >= since));
  const site = siteOf(page);
  const threshold = thresholdOf(watchFor);
  const priced = series.find((each) => each.kind === "number");
  const range = (
    <div className="ml-auto flex rounded-lg bg-accent p-[3px]" role="radiogroup">
      {RANGES.map((each) => (
        <button
          key={each.label}
          type="button"
          role="radio"
          aria-checked={days === each.days}
          className={cn(
            "h-6 rounded-md px-2 text-xs text-muted-foreground",
            days === each.days &&
              "bg-background text-foreground shadow-[0_1px_3px_rgba(30,27,46,.14)]",
          )}
          onClick={() => setDays(each.days)}
        >
          {each.label}
        </button>
      ))}
    </div>
  );
  return (
    <section
      id="schedule-chart"
      aria-label="Chart of tracked values"
      // The conversation stays in view under it.
      className="flex max-h-[45vh] flex-col gap-4 overflow-y-auto rounded-xl p-3.5 ring-1 ring-border"
    >
      {series.length === 0 && (
        <div className="flex items-center gap-2 text-[13px] text-muted-foreground">
          Nothing tracked in this time.
          {range}
        </div>
      )}
      {series.map((each, at) => (
        <div key={each.name} className="flex flex-col gap-2">
          <div className="flex min-h-7 items-center gap-2">
            <h3 className="text-[13px] font-semibold">
              {valueLabel(each.name)}
            </h3>
            <span className="truncate text-xs text-muted-foreground">
              {[each.unit, site].filter(Boolean).join(" · ")}
            </span>
            {at === 0 && range}
          </div>
          <ValueChart
            series={each}
            height={130}
            threshold={each === priced ? threshold : null}
            onPick={(line) => scrollToRun(line.run!.time)}
          />
        </div>
      ))}
    </section>
  );
}

// A watched page's card: the latest value, how it changed over the chart's
// days, and its chart, for the first value its runs track.
export function WatchChart({
  runs,
  watchFor,
}: {
  runs: Step[];
  watchFor: string;
}) {
  const series = chartSeries(runs)[0];
  if (!series) {
    return null;
  }
  if (series.kind === "yes") {
    return <YesNoStrip series={series} />;
  }
  const numbers = series.points.flatMap((point) =>
    typeof point.value === "number" ? [point] : [],
  );
  const first = numbers[0]!;
  const latest = numbers.at(-1)!;
  const change = (latest.value as number) - (first.value as number);
  const since = dayText(first.line.run!.time);
  const threshold = thresholdOf(watchFor);
  return (
    <div id="watch-chart" className="flex flex-col gap-2">
      <p className="flex items-baseline gap-2">
        <span className="text-[28px] font-semibold tabular-nums leading-none">
          {numberText(latest.value as number, series.unit)}
        </span>
        {numbers.length > 1 && (
          <span
            className={cn(
              "text-[13px] font-medium",
              change < 0 && watchFor.startsWith("Price drops")
                ? "text-success"
                : "text-muted-foreground",
            )}
          >
            {change === 0
              ? `No change since ${since}`
              : `${change < 0 ? "Down" : "Up"} ${numberText(Math.abs(change), series.unit)} since ${since}`}
          </span>
        )}
      </p>
      <LineChart series={series} height={56} threshold={threshold} />
    </div>
  );
}
