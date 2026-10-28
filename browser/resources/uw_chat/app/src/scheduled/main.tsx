// The Scheduled page, uw://scheduled: every watched page and scheduled chat,
// and a way to add each. See the frames "Scheduled page (Fluid)",
// "Scheduled page: watch a page (Fluid)", and "Scheduled page and Inbox:
// every state (Fluid)" on the canvas page "Agent chats", the canvas guide
// uw-scheduled-page-and-inbox, and docs/product.md, "Scheduled chats".
import "@/trusted-types";
import "@/theme-colors";
import { StrictMode, useEffect, useState, type ReactNode } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  Bell,
  CircleX,
  Clock,
  Eye,
  Globe,
  History,
  KeyRound,
  Loader2,
  MessageCircle,
  Pause,
  Play,
  Sparkles,
  Terminal as TerminalIcon,
  TriangleAlert,
  WifiOff,
  type LucideIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Dialog, DialogContent, DialogTitle } from "@/components/ui/dialog";
import { cn } from "@/lib/utils";
import {
  RunEnd,
  ScheduleRepeat,
  type ChatSchedule,
  type ScheduleState,
  type Step,
  type TrackedValue,
} from "../chat-api";
import { numberText } from "../chart";
import { ScheduleRows, WATCH_CHIPS } from "../schedule";
import type { WatchCardState } from "../watch-card/watch-card-api";
import { useModels, WatchForm, Watched } from "../watch-card/watch-parts";
import {
  connect,
  scheduledHandler,
  scheduledPage,
  type ScheduledRow,
  type WatchAddress,
} from "./scheduled-api";

const WEEKDAYS = [
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
];

const AMBER = "text-[#B4580A] dark:text-warning";

function clockText(ms: number): string {
  return new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
}

function daysFromToday(ms: number): number {
  const day = new Date(ms).setHours(0, 0, 0, 0);
  return Math.round((day - new Date().setHours(0, 0, 0, 0)) / 86_400_000);
}

function dateText(ms: number): string {
  return new Date(ms).toLocaleDateString([], {
    month: "short",
    day: "numeric",
  });
}

// "Today 9:02", "Tomorrow 9:00", "Yesterday 19:00", "Mon 9:00" within the
// week, or "Oct 24 10:00".
function dayAndTime(ms: number): string {
  const days = daysFromToday(ms);
  const day =
    days === 0
      ? "Today"
      : days === 1
        ? "Tomorrow"
        : days === -1
          ? "Yesterday"
          : Math.abs(days) < 7
            ? new Date(ms).toLocaleDateString([], { weekday: "short" })
            : dateText(ms);
  return `${day} ${clockText(ms)}`;
}

// How often it runs, under its next run: "Every day", "Every 4 hours".
function repeatText(schedule: ChatSchedule): string {
  switch (schedule.repeat) {
    case ScheduleRepeat.kEveryHours:
      return schedule.hours === 1
        ? "Every hour"
        : `Every ${schedule.hours} hours`;
    case ScheduleRepeat.kDaily:
      return "Every day";
    case ScheduleRepeat.kWeekdays:
      return "Weekdays";
    case ScheduleRepeat.kWeekly:
      return `Every ${WEEKDAYS[schedule.weekday]}`;
    default:
      return "Once";
  }
}

// The watch's chip icon, read back from what it watches for.
function watchIcon(watchFor: string): LucideIcon {
  return (
    WATCH_CHIPS.find((chip) => watchFor.startsWith(chip.label))?.icon ??
    Sparkles
  );
}

function sameValue(a: TrackedValue, b: TrackedValue): boolean {
  return a.number === b.number && a.yes === b.yes && a.text === b.text;
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

// The first value a watch's runs track, as it stands, and how it changed: a
// number by how much since the value before it, anything else since when.
function latestOf(
  runs: Step[],
  watchFor: string,
): { value: string; change: string; good: boolean } | null {
  const reported = runs.filter(
    (line) => line.run?.end === RunEnd.kReported && !line.run.counted,
  );
  const name = reported.find((line) => line.run!.values.length)?.run!
    .values[0]!.name;
  if (!name) {
    return null;
  }
  const points = reported.flatMap((line) => {
    const value = line.run!.values.find((each) => each.name === name);
    return value ? [{ time: line.run!.time, value }] : [];
  });
  const last = points.at(-1)!;
  let start = points.length - 1;
  while (start > 0 && sameValue(points[start - 1]!.value, last.value)) {
    start--;
  }
  const before = start > 0 ? points[start - 1]!.value : null;
  if (last.value.number !== null && before?.number != null) {
    const change = last.value.number - before.number;
    const unit = last.value.unit;
    return {
      value: shownValue(last.value),
      change: `${change < 0 ? "Down" : "Up"} ${numberText(Math.abs(change), unit)}, was ${numberText(before.number, unit)}`,
      good: change < 0 && watchFor.startsWith("Price drops"),
    };
  }
  return {
    value: shownValue(last.value),
    change: `Since ${dateText(points[start]!.time)}`,
    good: false,
  };
}

function Cell({
  top,
  under,
  className,
}: {
  top: ReactNode;
  under?: ReactNode;
  className?: string;
}) {
  return (
    <div className="flex min-w-0 flex-col gap-0.5">
      <b className={cn("truncate font-medium", className)}>{top}</b>
      {under && (
        <span className="flex min-w-0 items-center gap-1.5 truncate text-xs text-muted-foreground [&_svg]:size-3 [&_svg]:shrink-0">
          {under}
        </span>
      )}
    </div>
  );
}

// When it last ran, and how that went.
function LastRun({ row }: { row: ScheduledRow }) {
  const state = row.card.state;
  const run = row.card.runs.at(-1)?.run;
  const time = state.last || run?.time;
  const top = time ? dayAndTime(time) : "Not run yet";
  if (state.running) {
    return (
      <Cell
        top={top}
        under={
          <>
            <Loader2 aria-hidden className="animate-spin" />
            Running now
          </>
        }
      />
    );
  }
  if (state.waiting) {
    return (
      <Cell
        top={top}
        under={
          <span className={cn("flex items-center gap-1.5 font-medium", AMBER)}>
            <i className="size-[7px] rounded-full bg-warning" />
            Waiting for approval
          </span>
        }
      />
    );
  }
  if (state.signIn) {
    return (
      <Cell
        top={top}
        under={
          <span className={cn("flex items-center gap-1.5 font-medium", AMBER)}>
            <KeyRound aria-hidden />
            Sign in required
          </span>
        }
      />
    );
  }
  if (!run) {
    return <Cell top={top} />;
  }
  if (
    run.end === RunEnd.kFailed ||
    (run.end === RunEnd.kReported && run.counted)
  ) {
    return (
      <Cell
        top={top}
        under={
          <span className="flex items-center gap-1.5 font-medium text-destructive">
            <TriangleAlert aria-hidden />
            Didn't finish
          </span>
        }
      />
    );
  }
  if (run.end === RunEnd.kUnreachable) {
    return (
      <Cell
        top={top}
        under={
          <>
            <WifiOff aria-hidden />
            Couldn't reach the model
          </>
        }
      />
    );
  }
  if (run.end === RunEnd.kStopped) {
    return <Cell top={top} under="Stopped" />;
  }
  if (run.found) {
    return (
      <Cell
        top={top}
        under={
          <span className="flex items-center gap-1.5 font-medium text-primary">
            <Bell aria-hidden />
            Change alert
          </span>
        }
      />
    );
  }
  return <Cell top={top} under={run.what || "No change"} />;
}

// When it runs next, and how often.
function NextRun({ row }: { row: ScheduledRow }) {
  const state = row.card.state;
  if (!row.scheduled) {
    return (
      <Cell
        top="Trigger"
        under={<span className="truncate font-mono">{row.trigger}</span>}
      />
    );
  }
  if (state.paused) {
    return (
      <Cell
        top="Paused"
        className="text-muted-foreground"
        under={
          state.pauseReason ? (
            <span className="text-destructive">After 3 failed runs</span>
          ) : (
            <>
              <Pause aria-hidden />
              By you
            </>
          )
        }
      />
    );
  }
  const repeat = repeatText(state.schedule);
  if (state.waiting) {
    return <Cell top="After you answer" under={repeat} />;
  }
  if (state.schedule.trigger) {
    return (
      <Cell
        top={state.next ? dayAndTime(state.next) : "Trigger"}
        under={
          <span className="truncate font-mono">{state.schedule.trigger}</span>
        }
      />
    );
  }
  return <Cell top={state.next ? dayAndTime(state.next) : "—"} under={repeat} />;
}

// Open chat and Pause or Resume, which show while the pointer is on a row.
function RowButtons({
  row,
  withChat,
}: {
  row: ScheduledRow;
  withChat: boolean;
}) {
  const id = row.card.chatId;
  const paused = row.card.state.paused;
  const button =
    "grid size-7 place-items-center rounded-[7px] text-muted-foreground hover:bg-background hover:text-foreground hover:shadow-[0_1px_3px_rgba(30,27,46,.14)] [&_svg]:size-[15px]";
  return (
    <div className="flex justify-end gap-0.5 opacity-0 group-hover:opacity-100 group-focus-within:opacity-100">
      {withChat && (
        <button
          type="button"
          className={button}
          data-open-chat={id}
          title="Open chat"
          aria-label="Open chat"
          onClick={(event) => {
            event.stopPropagation();
            scheduledHandler.openChat(id);
          }}
        >
          <MessageCircle aria-hidden />
        </button>
      )}
      {row.scheduled && (
        <button
          type="button"
          className={button}
          data-pause={id}
          title={paused ? "Resume" : "Pause"}
          aria-label={paused ? "Resume" : "Pause"}
          onClick={(event) => {
            event.stopPropagation();
            scheduledHandler.setPaused(id, !paused);
          }}
        >
          {paused ? <Play aria-hidden /> : <Pause aria-hidden />}
        </button>
      )}
    </div>
  );
}

const COLUMNS =
  "grid grid-cols-[minmax(0,1.25fr)_minmax(0,1.05fr)_128px_148px_136px_60px] items-center gap-x-3.5";

function Table({ head, children }: { head: string[]; children: ReactNode }) {
  return (
    <div className="overflow-hidden rounded-xl border border-border">
      <div
        className={cn(
          COLUMNS,
          "h-8 bg-muted/40 pr-2.5 pl-3.5 text-[11.5px] font-medium text-muted-foreground",
        )}
      >
        {head.map((label, index) => (
          <span
            key={label}
            className={cn(index === 1 && head.length === 4 && "col-span-2")}
          >
            {label}
          </span>
        ))}
      </div>
      {children}
    </div>
  );
}

function Row({
  row,
  onClick,
  children,
}: {
  row: ScheduledRow;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <div
      role="button"
      tabIndex={0}
      data-row={row.card.chatId}
      className={cn(
        COLUMNS,
        "group h-[54px] cursor-default border-t border-border pr-2.5 pl-3.5 text-[13px] whitespace-nowrap outline-none hover:bg-accent focus-visible:bg-accent",
      )}
      onClick={onClick}
      onKeyDown={(event) => {
        if (event.key === "Enter") {
          onClick();
        }
      }}
    >
      {children}
    </div>
  );
}

function WatchedRow({ row }: { row: ScheduledRow }) {
  const card = row.card;
  const schedule = card.state.schedule;
  const latest = latestOf(card.runs, schedule.watchFor);
  const Icon = watchIcon(schedule.watchFor);
  return (
    <Row row={row} onClick={() => scheduledHandler.openPage(card.chatId)}>
      <div className="flex min-w-0 items-center gap-[11px]">
        {card.favicon ? (
          <img src={card.favicon} alt="" className="size-4 shrink-0" />
        ) : (
          <Globe aria-hidden className="size-4 shrink-0 text-muted-foreground" />
        )}
        <Cell
          top={card.title || schedule.page.replace(/^https?:\/\//, "")}
          under={card.host}
        />
      </div>
      <span className="flex min-w-0 items-center gap-[7px] text-muted-foreground">
        <Icon aria-hidden className="size-3.5 shrink-0 text-primary" />
        <span className="truncate">
          {schedule.watchFor || schedule.request}
        </span>
      </span>
      {latest ? (
        <Cell
          top={latest.value}
          className="text-sm font-semibold tabular-nums"
          under={
            <span className={cn(latest.good && "text-success")}>
              {latest.change}
            </span>
          }
        />
      ) : (
        <Cell top="—" className="text-muted-foreground" />
      )}
      <LastRun row={row} />
      <NextRun row={row} />
      <RowButtons row={row} withChat />
    </Row>
  );
}

function ChatRow({ row }: { row: ScheduledRow }) {
  const state = row.card.state;
  return (
    <Row row={row} onClick={() => scheduledHandler.openChat(row.card.chatId)}>
      <div className="flex min-w-0 items-center gap-[11px]">
        {row.scheduled ? (
          <Clock aria-hidden className="size-4 shrink-0 text-muted-foreground" />
        ) : (
          <TerminalIcon
            aria-hidden
            className="size-4 shrink-0 text-muted-foreground"
          />
        )}
        <Cell
          top={row.title || "New chat"}
          under={
            !row.scheduled
              ? "Trigger"
              : state.schedule.regularUpdates
                ? "Regular updates"
                : "Change alerts"
          }
        />
      </div>
      <span className="col-span-2 truncate text-muted-foreground">
        {row.scheduled
          ? state.schedule.request
          : "Runs when its trigger wakes it"}
      </span>
      <LastRun row={row} />
      <NextRun row={row} />
      <RowButtons row={row} withChat={false} />
    </Row>
  );
}

// Soonest next run first, then chats with no time, paused ones last.
function order(rows: ScheduledRow[]): ScheduledRow[] {
  const rank = (row: ScheduledRow) =>
    row.card.state.paused ? 2 : row.card.state.next ? 0 : 1;
  return [...rows].sort(
    (a, b) =>
      rank(a) - rank(b) ||
      (a.card.state.next || Infinity) - (b.card.state.next || Infinity),
  );
}

function summary(rows: ScheduledRow[]): string {
  const count = `${rows.length} scheduled ${rows.length === 1 ? "chat" : "chats"}.`;
  const next = Math.min(
    ...rows
      .filter((row) => !row.card.state.paused && row.card.state.next)
      .map((row) => row.card.state.next),
  );
  if (!Number.isFinite(next)) {
    return count;
  }
  const days = daysFromToday(next);
  const when =
    days === 0
      ? `at ${clockText(next)}`
      : days === 1
        ? `tomorrow at ${clockText(next)}`
        : `on ${dateText(next)} at ${clockText(next)}`;
  return `${count} The next run is ${when}.`;
}

// A card in the middle of the page, over the page dimmed.
function Card({
  label,
  open,
  onClose,
  children,
}: {
  label: string;
  open: boolean;
  onClose: () => void;
  children: ReactNode;
}) {
  return (
    <Dialog open={open} onOpenChange={(next) => !next && onClose()}>
      <DialogContent
        showCloseButton={false}
        aria-describedby={undefined}
        className="flex max-w-[424px] flex-col gap-2.5 p-4"
      >
        <DialogTitle className="sr-only">{label}</DialogTitle>
        {children}
      </DialogContent>
    </Dialog>
  );
}

// Watch a page: the watch card with an Address field on top.
function WatchAPage({
  state,
  onClose,
  onOpenWatch,
}: {
  state: ScheduleState;
  onClose: () => void;
  onOpenWatch: (chatId: string) => void;
}) {
  const [typed, setTyped] = useState("");
  const [found, setFound] = useState<WatchAddress | null>(null);
  const [left, setLeft] = useState(false);
  useEffect(() => {
    setFound(null);
    if (!typed.trim()) {
      return;
    }
    let current = true;
    const wait = setTimeout(() => {
      scheduledHandler.lookUpAddress(typed).then(({ address }) => {
        if (current) {
          setFound(address);
        }
      });
    }, 200);
    return () => {
      current = false;
      clearTimeout(wait);
    };
  }, [typed]);
  const invalid = left && !!typed.trim() && found !== null && !found.page;
  const card: WatchCardState = {
    title: found?.title ?? "",
    host: found?.host ?? "",
    favicon: found?.favicon ?? "",
    chatId: "",
    state,
    runs: [],
    frosted: false,
  };
  let line: ReactNode = null;
  if (invalid) {
    line = (
      <p className="flex items-center gap-1.5 text-[12.5px] text-destructive">
        <CircleX aria-hidden className="size-3.5 shrink-0" />
        Enter a web address, like fully.com.
      </p>
    );
  } else if (found?.chatId) {
    line = (
      <p
        id="watch-already"
        className="flex items-center gap-2 rounded-[9px] bg-primary/[0.06] px-2.5 py-1.5 text-[12.5px]"
      >
        <Eye aria-hidden className="size-3.5 shrink-0 text-primary" />
        Already watched.
        <button
          type="button"
          id="watch-open-watch"
          className="ml-auto font-medium text-primary"
          onClick={() => onOpenWatch(found.chatId)}
        >
          Open its watch
        </button>
      </p>
    );
  } else if (found?.page && found.title) {
    line = (
      <p
        id="watch-page"
        className="flex min-w-0 items-center gap-1.5 text-[12.5px] text-muted-foreground"
      >
        {found.favicon && (
          <img src={found.favicon} alt="" className="size-3.5 shrink-0" />
        )}
        <span className="truncate text-foreground">{found.title}</span>
        <span className="shrink-0">· {found.host}</span>
        <span title="From your history" className="ml-auto shrink-0">
          <History aria-hidden className="size-[13px]" />
        </span>
      </p>
    );
  } else if (found?.page) {
    line = (
      <p
        id="watch-page"
        className="flex items-center gap-1.5 text-[12.5px] text-muted-foreground"
      >
        <Globe aria-hidden className="size-[13px] shrink-0" />
        uw reads its title on the first run.
      </p>
    );
  }
  return (
    <WatchForm
      card={card}
      heading="Watch a page"
      blocked={!found?.page || !!found.chatId}
      top={
        <>
          <label
            htmlFor="watch-address"
            className="-mb-1 text-xs font-medium text-muted-foreground"
          >
            Address
          </label>
          <input
            id="watch-address"
            autoFocus
            placeholder="Paste or type an address"
            className={cn(
              "h-[34px] rounded-[9px] bg-background px-2.5 text-[13px] ring-2 outline-none",
              invalid ? "ring-destructive" : "ring-primary",
            )}
            value={typed}
            onChange={(event) => {
              setTyped(event.target.value);
              setLeft(false);
            }}
            onBlur={() => setLeft(true)}
          />
          {line}
        </>
      }
      onWatch={(schedule) => {
        if (found?.page) {
          scheduledHandler.watch({ ...schedule, page: found.page });
        }
        onClose();
      }}
      onCancel={onClose}
    />
  );
}

// A watched page's card, opened from Watch a page.
function OpenWatch({
  row,
  onClose,
}: {
  row: ScheduledRow;
  onClose: () => void;
}) {
  const [editing, setEditing] = useState(false);
  const since = Date.now() - 14 * 86_400_000;
  const card: WatchCardState = {
    ...row.card,
    runs: row.card.runs.filter((line) => (line.run?.time ?? 0) >= since),
  };
  if (editing) {
    return (
      <WatchForm
        card={card}
        onWatch={(schedule) => {
          scheduledHandler.watch(schedule);
          setEditing(false);
        }}
        onCancel={onClose}
      />
    );
  }
  return (
    <Watched
      card={card}
      onOpenChat={() => {
        scheduledHandler.openChat(card.chatId);
        onClose();
      }}
      onEdit={() => setEditing(true)}
      onStopWatching={() => {
        scheduledHandler.stopWatching(card.state.schedule.page);
        onClose();
      }}
    />
  );
}

// New scheduled chat: the schedule card with a Request field on top.
function NewScheduledChat({
  state,
  onClose,
}: {
  state: ScheduleState;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState<ChatSchedule>(state.schedule);
  const { providers, lists, openModels } = useModels(
    draft.model.providerId || undefined,
  );
  const request = draft.request.trim();
  return (
    <>
      <h2 className="flex items-center gap-2 text-[15px] font-semibold">
        <Clock aria-hidden className="size-[17px] text-primary" />
        New scheduled chat
      </h2>
      <label
        htmlFor="schedule-request"
        className="-mb-1 text-xs font-medium text-muted-foreground"
      >
        Request
      </label>
      <textarea
        id="schedule-request"
        autoFocus
        placeholder="What should each run do?"
        className="min-h-16 resize-none rounded-[9px] bg-background px-2.5 py-2 text-[13px] ring-2 ring-primary outline-none"
        value={draft.request}
        onChange={(event) =>
          setDraft((before) => ({ ...before, request: event.target.value }))
        }
      />
      <div className="my-1 h-px bg-border" />
      <ScheduleRows
        draft={draft}
        zoneName={state.zoneName}
        providers={providers}
        lists={lists}
        onOpenModels={openModels}
        onChange={(change) => setDraft((before) => ({ ...before, ...change }))}
        triggers={[]}
      />
      <div className="mt-1.5 flex items-center gap-2">
        <span className="mr-auto text-xs text-muted-foreground">
          Runs out of sight while uw is open
        </span>
        <Button
          id="schedule-cancel"
          variant="secondary"
          size="compact"
          onClick={onClose}
        >
          Cancel
        </Button>
        <Button
          id="schedule-save"
          variant="primary"
          size="compact"
          disabled={!request || !draft.model.model}
          onClick={() => {
            scheduledHandler.startScheduledChat({ ...draft, request });
            onClose();
          }}
        >
          Save schedule
        </Button>
      </div>
    </>
  );
}

type Open =
  | { kind: "watch"; state: ScheduleState }
  | { kind: "chat"; state: ScheduleState }
  | { kind: "watched"; chatId: string };

function ScheduledPage() {
  const [rows, setRows] = useState<ScheduledRow[] | null>(null);
  const [open, setOpen] = useState<Open | null>(null);
  useEffect(() => {
    const id = scheduledPage.onRows.addListener((next: ScheduledRow[]) =>
      setRows(next),
    );
    connect();
    return () => {
      scheduledPage.removeListener(id);
    };
  }, []);
  if (!rows) {
    return null;
  }
  const newCard = (kind: "watch" | "chat") =>
    scheduledHandler
      .newSchedule()
      .then(({ state }) => setOpen({ kind, state }));
  const add = (
    <>
      <Button
        id="new-scheduled-chat"
        variant="secondary"
        leadingIcon={Clock}
        onClick={() => newCard("chat")}
      >
        New scheduled chat
      </Button>
      <Button
        id="watch-a-page"
        variant="primary"
        leadingIcon={Eye}
        onClick={() => newCard("watch")}
      >
        Watch a page
      </Button>
    </>
  );
  const watched = order(rows.filter((row) => row.card.state.schedule.page));
  const chats = order(rows.filter((row) => !row.card.state.schedule.page));
  const scheduled = rows.filter((row) => row.scheduled);
  const allPaused =
    scheduled.length > 0 && scheduled.every((row) => row.card.state.paused);
  const watchedRow =
    open?.kind === "watched"
      ? rows.find((row) => row.card.chatId === open.chatId)
      : undefined;
  return (
    <main className="mx-auto flex max-w-[1020px] flex-col px-10 py-7">
      <header className="flex items-center gap-2">
        <div className="mr-auto">
          <h1 className="text-[22px] font-semibold tracking-[-0.012em]">
            Scheduled
          </h1>
          <p id="scheduled-summary" className="mt-0.5 text-muted-foreground">
            {rows.length ? summary(rows) : "Nothing scheduled"}
          </p>
        </div>
        {scheduled.length > 0 && (
          <Button
            id="pause-all"
            variant="secondary"
            leadingIcon={allPaused ? Play : Pause}
            onClick={() => scheduledHandler.pauseAll(!allPaused)}
          >
            {allPaused ? "Resume all" : "Pause all"}
          </Button>
        )}
        {rows.length > 0 && add}
      </header>
      {rows.length === 0 && (
        <div
          id="scheduled-empty"
          className="mt-5 flex flex-col items-center rounded-xl border border-dashed border-border px-6 py-7 text-center"
        >
          <span className="mb-2.5 grid size-10 place-items-center rounded-xl bg-accent text-muted-foreground">
            <Clock aria-hidden className="size-[18px]" />
          </span>
          <b className="text-[13.5px] font-semibold">
            Nothing runs on its own yet
          </b>
          <p className="mt-1 max-w-[330px] text-[12.5px] text-muted-foreground">
            Watch a page to hear when it changes, or start a chat that runs on
            a schedule.
          </p>
          <div className="mt-3.5 flex gap-2">{add}</div>
        </div>
      )}
      {watched.length > 0 && (
        <section id="watched-pages" className="mt-6">
          <h2 className="flex items-center gap-[7px] px-0.5 pb-2 text-[13px] font-semibold">
            <Eye aria-hidden className="size-[15px] text-muted-foreground" />
            Watched pages
            <span className="font-normal text-muted-foreground">
              {watched.length}
            </span>
          </h2>
          <Table
            head={["Page", "Watching for", "Latest", "Last run", "Next run", ""]}
          >
            {watched.map((row) => (
              <WatchedRow key={row.card.chatId} row={row} />
            ))}
          </Table>
        </section>
      )}
      {chats.length > 0 && (
        <section id="scheduled-chats" className="mt-6">
          <h2 className="flex items-center gap-[7px] px-0.5 pb-2 text-[13px] font-semibold">
            <Clock aria-hidden className="size-[15px] text-muted-foreground" />
            Scheduled chats
            <span className="font-normal text-muted-foreground">
              {chats.length}
            </span>
          </h2>
          <Table head={["Chat", "Asks for", "Last run", "Next run"]}>
            {chats.map((row) => (
              <ChatRow key={row.card.chatId} row={row} />
            ))}
          </Table>
        </section>
      )}
      <Card
        label={
          open?.kind === "chat"
            ? "New scheduled chat"
            : open?.kind === "watched"
              ? "Watching this page"
              : "Watch a page"
        }
        open={open !== null}
        onClose={() => setOpen(null)}
      >
        {open?.kind === "watch" && (
          <WatchAPage
            state={open.state}
            onClose={() => setOpen(null)}
            onOpenWatch={(chatId) => setOpen({ kind: "watched", chatId })}
          />
        )}
        {open?.kind === "chat" && (
          <NewScheduledChat state={open.state} onClose={() => setOpen(null)} />
        )}
        {watchedRow && (
          <OpenWatch row={watchedRow} onClose={() => setOpen(null)} />
        )}
      </Card>
    </main>
  );
}

// Fluid's parts read dark mode from a class on the root element.
const dark = matchMedia("(prefers-color-scheme: dark)");
const followScheme = () =>
  document.documentElement.classList.toggle("dark", dark.matches);
followScheme();
dark.addEventListener("change", followScheme);

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <MotionConfig reducedMotion="user">
      <ScheduledPage />
    </MotionConfig>
  </StrictMode>,
);
