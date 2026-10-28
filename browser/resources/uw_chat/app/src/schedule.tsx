// A scheduled chat on the chat page: the header's buttons, the strip under
// the title, the lines its runs add, and the schedule card. See the frames
// "Scheduled chat: the chat page" and "Scheduled chat: every state" on the
// canvas page "Agent chats", the "Watch this page" and "Watch from a chat"
// frames on the canvas page beside them, and the canvas guide
// uw-scheduled-chats.
import { useEffect, useState, type ReactNode } from "react";
import {
  Bell,
  BellOff,
  ChartLine,
  Check,
  ChevronDown,
  ChevronRight,
  Clock,
  Eye,
  Globe,
  Info,
  KeyRound,
  ListPlus,
  Loader2,
  Package,
  Pause,
  Pencil,
  Play,
  SkipForward,
  Sparkles,
  Square,
  Tag,
  Terminal as TerminalIcon,
  TriangleAlert,
  WifiOff,
  type LucideIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { cn } from "@/lib/utils";
import {
  RunEnd,
  ScheduleRepeat,
  StepKind,
  chatHandler,
  type ChatSchedule,
  type ScheduleState,
  type Step,
  type TriggerState,
} from "./chat-api";
import { valueText } from "./chart";
import { costText } from "./usage";
import { ModeChip } from "./mode-chip";
import { EffortChip, ModelChip } from "./providers/model-chip";
import type { ModelList, Provider } from "./providers/providers-api";

const WEEKDAYS = [
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
];

const EVERY_HOURS = [1, 2, 3, 4, 6, 12];

export type WatchPick = "price" | "stock" | "new" | "anything";

// The fixed "Watch for" chips. The watch card's pick becomes the chat's
// request, so no model fills in that card; the agent picks one by the same
// labels.
export const WATCH_CHIPS: { pick: WatchPick; label: string; icon: LucideIcon }[] =
  [
    { pick: "price", label: "Price drops", icon: Tag },
    { pick: "stock", label: "Back in stock", icon: Package },
    { pick: "new", label: "New items", icon: ListPlus },
    { pick: "anything", label: "Anything worth knowing", icon: Sparkles },
  ];

export function FilledEye({ className }: { className?: string }) {
  return (
    <svg aria-hidden viewBox="0 0 24 24" className={className}>
      <path
        fill="currentColor"
        d="M2.06 12.35a1 1 0 0 1 0-.7 10.75 10.75 0 0 1 19.88 0 1 1 0 0 1 0 .7 10.75 10.75 0 0 1-19.88 0"
      />
      <circle cx="12" cy="12" r="3" className="fill-background" />
    </svg>
  );
}

function clock(hour: number, minute: number): string {
  return `${hour}:${String(minute).padStart(2, "0")}`;
}

function shortTime(ms: number): string {
  return new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
}

// A time as the strip says it: "today 9:02", "tomorrow 9:00", or "Oct 9".
export function whenText(ms: number): string {
  const date = new Date(ms);
  const day = new Date(date).setHours(0, 0, 0, 0);
  const today = new Date().setHours(0, 0, 0, 0);
  const days = Math.round((day - today) / 86_400_000);
  if (days === 0) {
    return `today ${shortTime(ms)}`;
  }
  if (days === 1) {
    return `tomorrow ${shortTime(ms)}`;
  }
  if (days === -1) {
    return `yesterday ${shortTime(ms)}`;
  }
  return date.toLocaleDateString([], { month: "short", day: "numeric" });
}

export function describeSchedule(schedule: ChatSchedule): string {
  const at = clock(schedule.hour, schedule.minute);
  switch (schedule.repeat) {
    case ScheduleRepeat.kEveryHours:
      return schedule.hours === 1
        ? "Every hour"
        : `Every ${schedule.hours} hours`;
    case ScheduleRepeat.kDaily:
      return `Every day at ${at}`;
    case ScheduleRepeat.kWeekdays:
      return `Weekdays at ${at}`;
    case ScheduleRepeat.kWeekly:
      return `Every ${WEEKDAYS[schedule.weekday]} at ${at}`;
    default:
      return `Once, ${whenText(schedule.once)}`;
  }
}

export function ScheduleButtons({
  state,
  charted,
  chartOpen,
  onChart,
  onEdit,
}: {
  state: ScheduleState;
  // Its runs tracked a value that charts.
  charted: boolean;
  chartOpen: boolean;
  onChart: () => void;
  onEdit: () => void;
}) {
  return (
    <span id="schedule-buttons" className="ml-auto flex items-center gap-0.5">
      {charted && (
        <Button
          id="schedule-chart-button"
          variant="ghost"
          size="compact"
          leadingIcon={ChartLine}
          aria-pressed={chartOpen}
          className={cn(chartOpen && "bg-accent")}
          onClick={onChart}
        >
          Chart
        </Button>
      )}
      <Button
        id="schedule-edit"
        variant="ghost"
        size="compact"
        leadingIcon={Pencil}
        onClick={onEdit}
      >
        Edit
      </Button>
      <Button
        id="schedule-pause"
        variant="ghost"
        size="compact"
        leadingIcon={state.paused ? Play : Pause}
        onClick={() => chatHandler.setSchedulePaused(!state.paused)}
      >
        {state.paused ? "Resume" : "Pause"}
      </Button>
      <Button
        id="schedule-mute"
        variant="ghost"
        size="compact"
        leadingIcon={state.muted ? Bell : BellOff}
        onClick={() => chatHandler.setScheduleMuted(!state.muted)}
      >
        {state.muted ? "Unmute" : "Mute"}
      </Button>
    </span>
  );
}

function StripButton({
  id,
  primary,
  icon,
  onClick,
  children,
}: {
  id: string;
  primary?: boolean;
  icon?: typeof Play;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <Button
      id={id}
      variant={primary ? "primary" : "secondary"}
      size="compact"
      className="shrink-0"
      leadingIcon={icon}
      onClick={onClick}
    >
      {children}
    </Button>
  );
}

// How the schedule stands: when it runs, and anything wrong. A button at its
// end only when there is something to do.
export function ScheduleStrip({ state }: { state: ScheduleState }) {
  const schedule = describeSchedule(state.schedule);
  let tone = "bg-accent text-muted-foreground";
  let icon: ReactNode = null;
  let text: ReactNode;
  let action: ReactNode = null;
  if (state.paused && state.pauseReason) {
    tone = "bg-destructive/10 text-destructive";
    icon = <TriangleAlert className="size-3.5 shrink-0" />;
    text = (
      <>
        <b className="font-medium">Paused after 3 failed runs</b> ·{" "}
        {state.pauseReason}
      </>
    );
  } else if (state.paused) {
    icon = <Pause className="size-3.5 shrink-0" />;
    text = (
      <>
        <b className="font-medium text-foreground">Paused</b> · no runs until
        you resume
      </>
    );
  } else if (state.signIn) {
    tone = "bg-warning/15 text-foreground";
    icon = <KeyRound className="size-3.5 shrink-0" />;
    text = (
      <>
        <b className="font-medium">Sign in required</b> on {state.signIn}
      </>
    );
    action = (
      <StripButton
        id="schedule-sign-in"
        primary
        onClick={() => open(`https://${state.signIn}`)}
      >
        Sign in
      </StripButton>
    );
  } else if (state.waiting) {
    tone = "bg-warning/15 text-foreground";
    icon = <span className="mx-1 size-1.5 shrink-0 rounded-full bg-warning" />;
    text = (
      <>
        <b className="font-medium">A run waits for your approval</b> · later
        times are skipped
      </>
    );
  } else if (state.running) {
    icon = <Loader2 className="size-3.5 shrink-0 animate-spin" />;
    text = <b className="font-medium text-foreground">Running now</b>;
    action = (
      <StripButton
        id="schedule-stop"
        icon={Square}
        onClick={() => chatHandler.stopRun()}
      >
        Stop
      </StripButton>
    );
  } else if (state.muted) {
    icon = <BellOff className="size-3.5 shrink-0" />;
    text = (
      <>
        <b className="font-medium text-foreground">{schedule}</b> · Muted, runs
        and history go on
      </>
    );
  } else {
    text = (
      <>
        <b className="font-medium text-foreground">{schedule}</b>
        {state.next > 0 && ` · Next: ${whenText(state.next)}`}
        {state.last > 0 &&
          ` · Last: ${whenText(state.last)}${state.lastText ? `, ${state.lastText}` : ""}`}
      </>
    );
  }
  if (state.paused) {
    action = (
      <StripButton
        id="schedule-resume"
        primary
        icon={Play}
        onClick={() => chatHandler.setSchedulePaused(false)}
      >
        Resume
      </StripButton>
    );
  }
  return (
    <div
      id="schedule-strip"
      className={cn(
        "flex min-h-[34px] items-center gap-2 rounded-[10px] py-0.5 pr-1 pl-3 text-[12.5px]",
        tone,
      )}
    >
      {icon}
      <span className="min-w-0 flex-1 truncate">{text}</span>
      {action}
    </div>
  );
}

// A run that reported nothing worth telling the user.
export function isQuietRun(line: Step): boolean {
  return (
    line.kind === StepKind.kRun &&
    !!line.run &&
    line.run.end === RunEnd.kReported &&
    !line.run.what &&
    !line.run.counted
  );
}

// What a run cost, muted at the end of its line, when uw counted it.
function RunCost({ cost }: { cost: number | null }) {
  if (cost === null) {
    return null;
  }
  return (
    <span data-run-cost className="w-[52px] shrink-0 text-right text-[12.5px] tabular-nums text-muted-foreground">
      {costText(cost)}
    </span>
  );
}

function QuietLine({ line, inner }: { line: Step; inner?: boolean }) {
  const run = line.run!;
  return (
    <div
      data-run-time={run.time}
      className={cn(
        "quiet-run flex min-h-[26px] items-center gap-2 text-[12.5px] text-muted-foreground",
        inner && "pl-[22px]",
      )}
    >
      <Check aria-hidden className="size-3.5 shrink-0" />
      <span className="min-w-0 flex-1 truncate">
        {whenText(run.time)} · {line.text}
      </span>
      {run.values[0] && (
        <span className="shrink-0 tabular-nums text-foreground/80">
          {valueText(run.values[0])}
        </span>
      )}
      <RunCost cost={run.cost} />
    </div>
  );
}

// Quiet runs in a row fold into one line, which opens to show each.
export function QuietRuns({ runs }: { runs: Step[] }) {
  const [open, setOpen] = useState(false);
  if (runs.length === 1) {
    return <QuietLine line={runs[0]!} />;
  }
  const first = runs[0]!.run!;
  const last = runs.at(-1)!.run!;
  const Chevron = open ? ChevronDown : ChevronRight;
  const from = first.values[0] ? valueText(first.values[0]) : "";
  const to = last.values[0] ? valueText(last.values[0]) : "";
  const costs = runs.flatMap((line) => (line.run!.cost === null ? [] : [line.run!.cost]));
  const cost = costs.length ? costs.reduce((sum, each) => sum + each, 0) : null;
  return (
    <div
      className="quiet-runs flex flex-col"
      data-run-from={first.time}
      data-run-to={last.time}
    >
      <button
        type="button"
        className="flex min-h-[26px] items-center gap-2 text-left text-[12.5px] text-muted-foreground"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        <Chevron aria-hidden className="size-3.5 shrink-0" />
        <span className="min-w-0 flex-1 truncate">
          {whenText(first.time)} to {whenText(last.time)} · {runs.length} runs
          with nothing to tell
        </span>
        {from && (
          <span className="shrink-0 tabular-nums text-foreground/80">
            {from === to || !to ? from : `${from} to ${to}`}
          </span>
        )}
        <RunCost cost={cost} />
      </button>
      {open && runs.map((line, at) => <QuietLine key={at} line={line} inner />)}
    </div>
  );
}

function FoldedSteps({ steps }: { steps: string[] }) {
  const [open, setOpen] = useState(false);
  if (!steps.length) {
    return null;
  }
  const Chevron = open ? ChevronDown : ChevronRight;
  return (
    <div className="mt-1.5 text-[12.5px] text-muted-foreground">
      <button
        type="button"
        className="flex items-center gap-2"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        <Chevron aria-hidden className="size-3.5" />
        Show {steps.length} {steps.length === 1 ? "step" : "steps"}
      </button>
      {open && (
        <ol className="mt-1 flex flex-col gap-0.5 pl-[22px] font-mono text-[11.5px]">
          {steps.map((step, at) => (
            <li key={at} className="truncate">
              {step}
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}

// A run's line: a quiet line for a failure, or a message for what it found.
export function RunMessage({ line }: { line: Step }) {
  const run = line.run!;
  const at = whenText(run.time);
  const failure = (icon: ReactNode, text: ReactNode, className?: string) => (
    <div
      data-run-time={run.time}
      className={cn("run-line flex min-h-[26px] items-center gap-2 text-[12.5px]", className)}
    >
      {icon}
      <span className="min-w-0 flex-1 truncate">{text}</span>
      <RunCost cost={run.cost} />
    </div>
  );
  if (run.end === RunEnd.kStopped) {
    return failure(
      <Square aria-hidden className="size-3.5 shrink-0" />,
      `${at} · You stopped the run.`,
      "text-muted-foreground",
    );
  }
  if (run.end === RunEnd.kUnreachable) {
    return failure(
      <WifiOff aria-hidden className="size-3.5 shrink-0" />,
      `${at} · ${line.text} · not counted`,
      "text-muted-foreground",
    );
  }
  if (run.counted) {
    return (
      <>
        {failure(
          run.end === RunEnd.kReported ? (
            <KeyRound aria-hidden className="size-3.5 shrink-0 text-warning" />
          ) : (
            <TriangleAlert aria-hidden className="size-3.5 shrink-0 text-destructive" />
          ),
          `${at} · ${run.end === RunEnd.kReported ? line.text : `Didn't finish · ${line.text}`}`,
          "text-foreground/80",
        )}
        {run.paused &&
          failure(
            <Pause aria-hidden className="size-3.5 shrink-0 text-destructive" />,
            `${at} · Paused after 3 failed runs`,
            "text-foreground/80",
          )}
      </>
    );
  }
  const alert = run.what === "Change alert";
  return (
    <div
      data-run-time={run.time}
      className={cn(
        "run-message text-sm leading-relaxed",
        alert && "rounded-xl bg-primary/[0.06] px-3 py-2.5",
      )}
    >
      <div className="flex items-center gap-1.5 text-xs text-muted-foreground">
        {alert ? (
          <Bell aria-hidden className="size-3.5 text-primary" />
        ) : run.trigger ? (
          <TerminalIcon aria-hidden className="size-3.5" />
        ) : (
          <Clock aria-hidden className="size-3.5" />
        )}
        {at}
        {run.trigger && (
          <>
            {" · "}
            <code className="rounded bg-accent px-1 font-mono text-[11px]">
              {run.trigger}
            </code>
          </>
        )}
        {run.what && (
          <span
            className={cn(
              "rounded-full px-[7px] py-px font-medium",
              alert ? "bg-primary/[0.12] text-primary" : "bg-accent text-muted-foreground",
            )}
          >
            {run.what}
          </span>
        )}
        <span className="ml-auto">
          <RunCost cost={run.cost} />
        </span>
      </div>
      <p className="mt-1">{line.text}</p>
      {run.values.length > 0 && (
        <p className="mt-0.5 text-xs text-muted-foreground tabular-nums">
          {run.values.map(valueText).join(" · ")}
        </p>
      )}
      <FoldedSteps steps={run.steps} />
    </div>
  );
}

export function SkippedLine({ count }: { count: string }) {
  return (
    <div className="flex min-h-[26px] items-center gap-2 text-[12.5px] text-muted-foreground">
      <SkipForward aria-hidden className="size-3.5 shrink-0" />
      Skipped {count} {count === "1" ? "run" : "runs"} while this waited for
      you
    </div>
  );
}

const REPEATS: { label: string; repeat: ScheduleRepeat; hours?: number }[] = [
  ...EVERY_HOURS.map((hours) => ({
    label: hours === 1 ? "Every hour" : `Every ${hours} hours`,
    repeat: ScheduleRepeat.kEveryHours,
    hours,
  })),
  { label: "Every day", repeat: ScheduleRepeat.kDaily },
  { label: "Weekdays", repeat: ScheduleRepeat.kWeekdays },
  { label: "Every week", repeat: ScheduleRepeat.kWeekly },
  { label: "Once", repeat: ScheduleRepeat.kOnce },
];

const FIELD =
  "h-8 min-w-0 rounded-lg bg-accent px-2.5 text-[13px] text-foreground outline-none focus-visible:ring-2 focus-visible:ring-ring";

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="grid grid-cols-[76px_1fr] items-center gap-2.5">
      <span className="text-[13px] text-muted-foreground">{label}</span>
      <div className="flex min-w-0 items-center gap-1.5">{children}</div>
    </div>
  );
}

function localDateTime(ms: number): string {
  const date = new Date(ms - new Date(ms).getTimezoneOffset() * 60_000);
  return date.toISOString().slice(0, 16);
}

// The schedule card's rows, from Repeat to Approvals. The watch card shares
// them.
export function ScheduleRows({
  draft,
  zoneName,
  providers,
  lists,
  onOpenModels,
  onChange: set,
  triggers,
}: {
  draft: ChatSchedule;
  // The Mac's time zone, as a person reads it.
  zoneName: string;
  providers: Provider[];
  lists: Record<string, ModelList | undefined>;
  onOpenModels: () => void;
  onChange: (change: Partial<ChatSchedule>) => void;
  // On a chat's card: the commands its Trigger row offers besides None.
  triggers?: string[];
}) {
  const [changingZone, setChangingZone] = useState(false);
  const repeatIndex = REPEATS.findIndex(
    (each) =>
      each.repeat === draft.repeat &&
      (each.hours === undefined || each.hours === draft.hours),
  );
  const model = draft.model;
  const chosenModel = lists[model.providerId]?.models.find(
    (each) => each.name === model.model,
  );
  const chosen = providers.find((provider) => provider.id === model.providerId);
  const wallTime =
    draft.repeat === ScheduleRepeat.kDaily ||
    draft.repeat === ScheduleRepeat.kWeekdays ||
    draft.repeat === ScheduleRepeat.kWeekly;
  const zones =
    typeof Intl.supportedValuesOf === "function"
      ? Intl.supportedValuesOf("timeZone")
      : [];
  return (
    <>
      <Row label="Repeat">
        <select
          id="schedule-repeat"
          className={cn(FIELD, "flex-1")}
          value={repeatIndex}
          onChange={(event) => {
            const picked = REPEATS[Number(event.target.value)]!;
            set({
              repeat: picked.repeat,
              hours: picked.hours ?? draft.hours,
              once:
                picked.repeat === ScheduleRepeat.kOnce && !draft.once
                  ? Date.now() + 86_400_000
                  : draft.once,
            });
          }}
        >
          {REPEATS.map((each, index) => (
            <option key={each.label} value={index}>
              {each.label}
            </option>
          ))}
        </select>
        {draft.repeat === ScheduleRepeat.kWeekly && (
          <select
            id="schedule-weekday"
            className={FIELD}
            value={draft.weekday}
            onChange={(event) => set({ weekday: Number(event.target.value) })}
          >
            {WEEKDAYS.map((day, index) => (
              <option key={day} value={index}>
                {day.slice(0, 3)}
              </option>
            ))}
          </select>
        )}
        {wallTime && (
          <input
            id="schedule-time"
            type="time"
            className={FIELD}
            value={`${String(draft.hour).padStart(2, "0")}:${String(draft.minute).padStart(2, "0")}`}
            onChange={(event) => {
              const [hour, minute] = event.target.value.split(":").map(Number);
              set({ hour: hour ?? 9, minute: minute ?? 0 });
            }}
          />
        )}
        {draft.repeat === ScheduleRepeat.kOnce && (
          <input
            id="schedule-once"
            type="datetime-local"
            className={FIELD}
            value={localDateTime(draft.once)}
            onChange={(event) =>
              set({ once: new Date(event.target.value).getTime() })
            }
          />
        )}
      </Row>
      {wallTime && (
        <p className="-mt-1 pl-[86px] text-xs text-muted-foreground">
          {changingZone ? (
            <select
              id="schedule-zone"
              aria-label="Time zone"
              className={cn(FIELD, "h-7 w-full")}
              value={draft.timeZone}
              onChange={(event) => set({ timeZone: event.target.value })}
            >
              <option value="">Your time zone, {zoneName}</option>
              {zones.map((zone) => (
                <option key={zone} value={zone}>
                  {zone.replaceAll("_", " ")}
                </option>
              ))}
            </select>
          ) : (
            <>
              {draft.timeZone
                ? `${draft.timeZone.split("/").at(-1)!.replaceAll("_", " ")} time.`
                : `Your time zone, ${zoneName}.`}{" "}
              <button
                type="button"
                id="schedule-change-zone"
                className="font-medium text-primary"
                onClick={() => setChangingZone(true)}
              >
                Change
              </button>
            </>
            )}
          </p>
        )}
        {triggers && (
          <Row label="Trigger">
            <TerminalIcon aria-hidden className="size-4 shrink-0 text-muted-foreground" />
            <select
              id="schedule-trigger"
              className={cn(FIELD, "flex-1 font-mono text-[12px]")}
              value={draft.trigger}
              onChange={(event) => set({ trigger: event.target.value })}
            >
              <option value="">None</option>
              {triggers.map((command) => (
                <option key={command} value={command}>
                  {command}
                </option>
              ))}
            </select>
          </Row>
        )}
        <Row label="Tell me">
          <div className="flex flex-1 rounded-[9px] bg-accent p-[3px]" role="radiogroup">
            {[
              { label: "Change alerts", regular: false },
              { label: "Regular updates", regular: true },
            ].map((each) => (
              <button
                key={each.label}
                type="button"
                role="radio"
                aria-checked={draft.regularUpdates === each.regular}
                className={cn(
                  "h-[26px] flex-1 rounded-[7px] text-xs font-medium text-muted-foreground",
                  draft.regularUpdates === each.regular &&
                    "bg-background text-foreground shadow-[0_1px_3px_rgba(30,27,46,.14)]",
                )}
                onClick={() => set({ regularUpdates: each.regular })}
              >
                {each.label}
              </button>
            ))}
          </div>
        </Row>
        {!draft.regularUpdates && (
          <div className="-mt-0.5 pl-[86px] text-[12.5px] text-muted-foreground">
            <Switch
              id="schedule-even-when"
              label="Even when nothing changes"
              size="compact"
              className="w-full"
              checked={draft.evenWhenNothingChanges}
              onToggle={() =>
                set({ evenWhenNothingChanges: !draft.evenWhenNothingChanges })
              }
            />
          </div>
        )}
        <Row label="Model">
          <ModelChip
            choice={model.model ? model : null}
            providers={providers}
            lists={lists}
            needsTools
            className="h-8 min-w-0 flex-1"
            onOpen={onOpenModels}
            onPick={(provider, name) =>
              set({ model: { providerId: provider.id, model: name, effort: "" } })
            }
          />
          <EffortChip
            efforts={chosenModel?.efforts ?? []}
            effort={model.effort}
            model={model.model}
            preset={chosen?.preset}
            className="h-8"
            onPick={(effort) => set({ model: { ...model, effort } })}
          />
        </Row>
        <p className="-mt-1 pl-[86px] text-xs text-muted-foreground">
          For runs. Replies use the chat's model.
        </p>
        <Row label="Approvals">
          <ModeChip
            mode={draft.mode}
            className="h-8"
            onPick={(mode) => set({ mode })}
          />
        </Row>
    </>
  );
}

// The page a chat's card watches: its icon and title from an open tab or
// history, else its site and path.
function WatchedPage({ page }: { page: string }) {
  const [known, setKnown] = useState({ title: "", favicon: "" });
  useEffect(() => {
    let current = true;
    chatHandler.describePage(page).then((found) => {
      if (current) {
        setKnown(found);
      }
    });
    return () => {
      current = false;
    };
  }, [page]);
  const url = new URL(page);
  const host = url.hostname.replace(/^www\./, "");
  return (
    <p
      id="schedule-page"
      className="-mt-1.5 flex min-w-0 items-center gap-1.5 text-[12.5px] text-muted-foreground"
    >
      {known.favicon ? (
        <img src={known.favicon} alt="" className="size-3.5 shrink-0 rounded-[3px]" />
      ) : (
        <Globe aria-hidden className="size-3.5 shrink-0" />
      )}
      {known.title ? (
        <>
          <span className="truncate text-foreground">{known.title}</span>{" "}
          <span className="shrink-0">· {host}</span>
        </>
      ) : (
        <>
          <span className="shrink-0 text-foreground">{host}</span>
          <span className="truncate">
            {url.pathname === "/" ? "" : url.pathname}
            {url.search}
          </span>
        </>
      )}
    </p>
  );
}

// "Run this chat again": the card that makes or changes a chat's schedule.
// With a page, which only the agent fills in, it is the chat's watch of that
// page. Nothing is saved until the user presses Save schedule or Watch.
export function ScheduleCard({
  state,
  editing,
  watching,
  providers,
  lists,
  onOpenModels,
  onClose,
  trigger,
}: {
  state: ScheduleState;
  // The chat has a schedule already.
  editing: boolean;
  // The chat already watches the card's page.
  watching: boolean;
  providers: Provider[];
  lists: Record<string, ModelList | undefined>;
  onOpenModels: () => void;
  onClose: () => void;
  // The chat's running trigger, which the card can keep.
  trigger: TriggerState | null;
}) {
  const [draft, setDraft] = useState<ChatSchedule>(state.schedule);
  const triggers = [
    ...new Set(
      [state.schedule.trigger, trigger?.command ?? ""].filter(Boolean),
    ),
  ];
  const set = (change: Partial<ChatSchedule>) =>
    setDraft((before) => ({ ...before, ...change }));
  const save = () => {
    if (!draft.request.trim()) {
      return;
    }
    chatHandler.saveSchedule(draft);
    onClose();
  };
  const page = state.schedule.page;
  const title = !page
    ? "Run this chat again"
    : watching
      ? "Watching this page"
      : "Watch this page";
  const ChipIcon =
    WATCH_CHIPS.find((chip) => state.schedule.watchFor.startsWith(chip.label))
      ?.icon ?? Tag;
  return (
    <div
      id="schedule-card"
      role="dialog"
      aria-label={title}
      className="absolute top-12 right-7 z-20 flex w-96 flex-col gap-2.5 rounded-[14px] bg-background p-4 shadow-[0_0_0_1px_rgba(30,27,46,.08),0_18px_48px_rgba(30,27,46,.22)]"
    >
      <h2 className="flex items-center gap-2 text-[15px] font-semibold">
        {!page ? (
          <Clock aria-hidden className="size-[17px] text-primary" />
        ) : watching ? (
          <FilledEye className="size-[17px] text-primary" />
        ) : (
          <Eye aria-hidden className="size-[17px] text-primary" />
        )}
        {title}
      </h2>
      {page && <WatchedPage page={page} />}
      {state.schedule.watchFor && (
        <>
          <p className="text-xs font-semibold text-muted-foreground">
            Watch for
          </p>
          <p
            id="schedule-watch-for"
            className="-mt-1 flex h-8 items-center gap-2 rounded-[9px] bg-primary/[0.08] px-2.5 text-[12.5px] font-medium text-primary"
          >
            <ChipIcon aria-hidden className="size-3.5 shrink-0" />
            {state.schedule.watchFor}
          </p>
        </>
      )}
      {page && (
        <p className="-mt-1 flex items-center gap-1.5 text-xs text-muted-foreground">
          <Info aria-hidden className="size-3.5 shrink-0" />
          {state.schedule.watchFor
            ? "To change the page or what it watches for, ask in the chat."
            : "To change the page, ask in the chat."}
        </p>
      )}
      <textarea
        id="schedule-request"
        aria-label="What each run is asked"
        className="min-h-14 resize-none rounded-[9px] bg-accent px-2.5 py-2 text-[13px] outline-none focus-visible:ring-2 focus-visible:ring-ring"
        value={draft.request}
        onChange={(event) => set({ request: event.target.value })}
      />
      <div className="my-1 h-px bg-border" />
      <ScheduleRows
        draft={draft}
        zoneName={state.zoneName}
        providers={providers}
        lists={lists}
        onOpenModels={onOpenModels}
        onChange={set}
        triggers={triggers}
      />
      <div className="mt-1.5 flex items-center gap-2">
        {editing && (
          <Button
            id="schedule-remove"
            variant="ghost"
            size="compact"
            className="mr-auto text-destructive"
            onClick={() => {
              chatHandler.removeSchedule();
              onClose();
            }}
          >
            {watching ? "Stop watching" : "Remove schedule"}
          </Button>
        )}
        <Button
          id="schedule-cancel"
          variant="secondary"
          size="compact"
          className={cn(!editing && "ml-auto")}
          onClick={onClose}
        >
          Cancel
        </Button>
        <Button
          id="schedule-save"
          variant="primary"
          size="compact"
          disabled={!draft.request.trim() || !draft.model.model}
          onClick={save}
        >
          {page && !watching ? "Watch" : "Save schedule"}
        </Button>
      </div>
    </div>
  );
}
