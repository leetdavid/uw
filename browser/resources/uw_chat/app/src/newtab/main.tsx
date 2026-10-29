// uw's new tab page: a box to search or ask, and the state of uw's features
// around it. See docs/plans/new-tab-page.md and the frames "New tab page: a
// busy day" and "New tab page: every state" on the canvas page "Agent chats".
import "@/trusted-types";
import "@/theme-colors";
import {
  StrictMode,
  useEffect,
  useRef,
  useState,
  type KeyboardEvent,
  type ReactNode,
} from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  ArrowUp,
  ChevronRight,
  Clock,
  Flag,
  Globe,
  Hand,
  Search,
  Terminal as TerminalIcon,
  X,
  type LucideIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Dialog, DialogContent, DialogTitle } from "@/components/ui/dialog";
import { cn } from "@/lib/utils";
import { KIND_COLORS } from "../alert-kinds";
import type { ChatSchedule, ScheduleState } from "../chat-api";
import { CHIP, ModelChip } from "../providers/model-chip";
import { providersHandler } from "../providers/providers-api";
import { repeatText, ScheduleRows } from "../schedule";
import { useModels } from "../watch-card/watch-parts";
import {
  connect,
  newTabHandler,
  newTabPage,
  NO_ROW,
  open,
  query,
  searchPage,
  type AutocompleteMatch,
  type AutocompleteResult,
  type NewTabBox,
  type NewTabRow,
  type NewTabSections,
} from "./newtab-api";

type Mode = "search" | "ask";

// The browser names each row's kind in its type, as in the command bar.
const KINDS: Record<string, string> = {
  tab: "Tab",
  bookmark: "Bookmark",
  history: "History",
  search: "Search",
};

// The chat bubble with a sparkle that uw's chats wear.
function AskIcon({ className }: { className?: string }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.75}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
      className={cn("size-4", className)}
    >
      <path d="M7.9 20A9 9 0 1 0 4 16.1L2 22Z" />
      <path d="M12 8.2l.95 2.85 2.85.95-2.85.95L12 15.8l-.95-2.85L8.2 12l2.85-.95z" />
    </svg>
  );
}

// A page reads as its title and site, a search as its words.
function describe(match: AutocompleteMatch): [string, string] {
  if (match.type === "search" || !match.description) {
    return [match.contents, ""];
  }
  const url = match.destinationUrl;
  const site = URL.canParse(url)
    ? new URL(url).host.replace(/^www\./, "")
    : "";
  return [match.description, site];
}

function Switch({
  mode,
  canAsk,
  onMode,
}: {
  mode: Mode;
  canAsk: boolean;
  onMode: (mode: Mode) => void;
}) {
  const segment = (value: Mode, label: string, icon: ReactNode) => (
    <button
      type="button"
      id={`newtab-${value}`}
      disabled={value === "ask" && !canAsk}
      title={value === "ask" && !canAsk ? "Add an AI provider to ask" : undefined}
      aria-pressed={mode === value}
      onMouseDown={(event) => event.preventDefault()}
      onClick={() => onMode(value)}
      className={cn(
        "inline-flex h-7 items-center gap-1.5 rounded-lg px-3 font-medium text-muted-foreground outline-none disabled:opacity-40",
        mode === value && "bg-background text-foreground shadow-[0_1px_3px_rgba(30,27,46,.14)]",
      )}
    >
      {icon}
      {label}
    </button>
  );
  return (
    <div className="flex shrink-0 rounded-[10px] bg-muted p-[3px]">
      {segment("search", "Search", <Search aria-hidden className="size-3.5" />)}
      {segment("ask", "Ask", <AskIcon className="size-3.5" />)}
    </div>
  );
}

function Suggestions({
  rows,
  selected,
  onHover,
  onPick,
}: {
  rows: { match: AutocompleteMatch; line: number }[];
  selected: number;
  onHover: (index: number) => void;
  onPick: (index: number) => void;
}) {
  return (
    <ul
      id="newtab-rows"
      role="listbox"
      className="border-t border-border p-1.5"
    >
      {rows.map(({ match }, index) => {
        const [title, detail] = describe(match);
        return (
          <li
            key={index}
            role="option"
            aria-selected={index === selected}
            onMouseMove={() => index !== selected && onHover(index)}
            onMouseDown={(event) => event.preventDefault()}
            onClick={() => onPick(index)}
            className={cn(
              "flex h-[38px] cursor-default items-center gap-2.5 rounded-[9px] px-2.5 whitespace-nowrap",
              index === selected && "bg-muted",
            )}
          >
            {match.type === "search" ? (
              <Search aria-hidden className="size-4 text-muted-foreground" />
            ) : (
              <img
                alt=""
                className="size-4"
                src={`chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${encodeURIComponent(match.destinationUrl)}`}
              />
            )}
            <span className="truncate font-medium">{title}</span>
            <span className="min-w-0 flex-1 truncate text-xs text-muted-foreground">
              {detail}
            </span>
            <span className="text-[11.5px] text-muted-foreground">
              {KINDS[match.type] ?? ""}
            </span>
          </li>
        );
      })}
    </ul>
  );
}

function ScheduleCard({
  request,
  onClose,
}: {
  request: string;
  onClose: () => void;
}) {
  const [state, setState] = useState<ScheduleState | null>(null);
  const [draft, setDraft] = useState<ChatSchedule | null>(null);
  const { providers, lists, openModels } = useModels(
    draft?.model.providerId || undefined,
  );
  useEffect(() => {
    newTabHandler.newSchedule().then(({ state }) => {
      setState(state);
      setDraft(state.schedule);
    });
  }, []);
  if (!state || !draft) {
    return null;
  }
  return (
    <>
      <h2 className="flex items-center gap-2 text-[15px] font-semibold">
        <Clock aria-hidden className="size-[17px] text-primary" />
        Run this chat again
      </h2>
      <p className="truncate text-[13px] text-muted-foreground">{request}</p>
      <div className="my-1 h-px bg-border" />
      <ScheduleRows
        draft={draft}
        zoneName={state.zoneName}
        providers={providers}
        lists={lists}
        onOpenModels={openModels}
        onChange={(change) =>
          setDraft((before) => before && { ...before, ...change })
        }
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
          disabled={!draft.model.model}
          onClick={() => newTabHandler.askOnSchedule({ ...draft, request })}
        >
          Save schedule
        </Button>
      </div>
    </>
  );
}

function Box({ box }: { box: NewTabBox }) {
  const [mode, setMode] = useState<Mode>("search");
  const [text, setText] = useState("");
  const [rows, setRows] = useState<{ match: AutocompleteMatch; line: number }[]>([]);
  const [selected, setSelected] = useState(0);
  const [choice, setChoice] = useState<{ providerId: string; model: string } | null>(null);
  const [scheduling, setScheduling] = useState(false);
  const field = useRef<HTMLInputElement & HTMLTextAreaElement>(null);
  const typed = useRef("");
  // Lists the provider's models only once Ask shows, so a new tab asks the
  // provider nothing.
  const { providers, lists, openModels } = useModels(
    mode === "ask" ? choice?.providerId : undefined,
  );

  useEffect(() => {
    const id = searchPage.autocompleteResultChanged.addListener(
      (result: AutocompleteResult) => {
        if (result.input !== typed.current) {
          return;
        }
        setRows(
          result.matches
            .map((match, line) => ({ match, line }))
            .filter((row) => !row.match.isHidden),
        );
        setSelected(0);
      },
    );
    providersHandler.getState().then(({ state }) => {
      const agent = state.agent ?? state.language;
      setChoice(agent ? { providerId: agent.providerId, model: agent.model } : null);
    });
    return () => {
      searchPage.removeListener(id);
    };
  }, []);

  useEffect(() => {
    field.current?.focus();
  }, [mode]);

  const changeMode = (next: Mode) => {
    if (next === "ask" && !box.canAsk) {
      return;
    }
    setMode(next);
    setRows([]);
    query("", 0);
  };

  const onChange = (value: string) => {
    setText(value);
    typed.current = value;
    if (mode === "search") {
      if (!value) {
        setRows([]);
      }
      query(value, field.current?.selectionStart ?? value.length);
    }
  };

  const go = () => {
    const words = text.trim();
    if (mode === "ask") {
      if (words) {
        newTabHandler.ask(words, choice?.providerId ?? "", choice?.model ?? "");
      }
      return;
    }
    const row = rows[selected];
    if (row) {
      open(row.line, row.match.destinationUrl);
    } else if (words) {
      open(NO_ROW, "");
    }
  };

  const onKeyDown = (event: KeyboardEvent) => {
    if (event.key === "Tab" && !event.shiftKey && !event.altKey && !event.metaKey) {
      event.preventDefault();
      changeMode(mode === "search" ? "ask" : "search");
    } else if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      go();
    } else if (mode === "search" && event.key === "ArrowDown" && rows.length) {
      event.preventDefault();
      setSelected((selected + 1) % rows.length);
    } else if (mode === "search" && event.key === "ArrowUp" && rows.length) {
      event.preventDefault();
      setSelected((selected - 1 + rows.length) % rows.length);
    } else if (event.key === "Escape") {
      onChange("");
    }
  };

  const shared = {
    ref: field,
    id: "newtab-field",
    value: text,
    autoFocus: true,
    spellCheck: false,
    onChange: (event: { target: { value: string } }) => onChange(event.target.value),
    onKeyDown,
  };

  return (
    <div
      id="newtab-box"
      className="w-full max-w-[760px] overflow-hidden rounded-2xl bg-background shadow-[0_0_0_1px_rgba(30,27,46,.12),0_8px_24px_rgba(30,27,46,.08)] dark:shadow-[0_0_0_1px_#262626]"
    >
      {mode === "search" ? (
        <>
          <div className="flex h-14 items-center gap-2.5 pr-2.5 pl-[18px]">
            <Search aria-hidden className="size-[17px] text-muted-foreground" />
            <input
              {...shared}
              placeholder="Search or type an address"
              className="min-w-0 flex-1 bg-transparent text-base outline-none placeholder:text-muted-foreground"
            />
            <Switch mode={mode} canAsk={box.canAsk} onMode={changeMode} />
          </div>
          {text && rows.length > 0 && (
            <Suggestions
              rows={rows}
              selected={selected}
              onHover={setSelected}
              onPick={(index) =>
                open(rows[index]!.line, rows[index]!.match.destinationUrl)
              }
            />
          )}
        </>
      ) : (
        <div className="px-2.5 pt-4 pb-2.5 pl-[18px]">
          <textarea
            {...shared}
            rows={2}
            placeholder="Ask uw to do something"
            className="block h-[46px] w-full resize-none bg-transparent text-base outline-none placeholder:text-muted-foreground"
          />
          <div className="-ml-2 flex items-center gap-1.5">
            <ModelChip
              choice={choice}
              providers={providers}
              lists={lists}
              needsTools
              manage
              onOpen={openModels}
              onPick={(provider, model) => setChoice({ providerId: provider.id, model })}
            />
            {!box.privateWindow && (
              <button
                type="button"
                id="newtab-once"
                className={CHIP}
                onClick={() => text.trim() && setScheduling(true)}
              >
                <Clock aria-hidden className="size-3.5 text-muted-foreground" />
                Once
              </button>
            )}
            <span className="ml-auto" />
            <Switch mode={mode} canAsk={box.canAsk} onMode={changeMode} />
            <button
              type="button"
              id="newtab-send"
              aria-label="Ask"
              disabled={!text.trim()}
              onClick={go}
              className="ml-1.5 grid size-[34px] place-items-center rounded-full bg-primary text-primary-foreground disabled:opacity-40"
            >
              <ArrowUp aria-hidden className="size-4" />
            </button>
          </div>
        </div>
      )}
      <Dialog open={scheduling} onOpenChange={(next) => !next && setScheduling(false)}>
        <DialogContent
          showCloseButton={false}
          aria-describedby={undefined}
          className="flex max-w-[424px] flex-col gap-2.5 p-4"
        >
          <DialogTitle className="sr-only">Run this chat again</DialogTitle>
          {scheduling && (
            <ScheduleCard request={text.trim()} onClose={() => setScheduling(false)} />
          )}
        </DialogContent>
      </Dialog>
    </div>
  );
}

// "9:02" today, else with its day: "Tomorrow 7:30", "Yesterday 19:00", or
// "Oct 24 10:00".
function rowTime(ms: number): string {
  const clock = new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
  const day = new Date(ms).setHours(0, 0, 0, 0);
  const days = Math.round((day - new Date().setHours(0, 0, 0, 0)) / 86_400_000);
  if (days === 0) {
    return clock;
  }
  const name =
    days === 1
      ? "Tomorrow"
      : days === -1
        ? "Yesterday"
        : new Date(ms).toLocaleDateString([], { month: "short", day: "numeric" });
  return `${name} ${clock}`;
}

// How a run comes up: what it looks for and how often, or the trigger that
// wakes it.
function comingUpText(row: NewTabRow): ReactNode {
  const schedule = row.schedule;
  if (!schedule) {
    return (
      <>
        When <code className="font-mono text-[11.5px]">{row.trigger}</code> wakes it
      </>
    );
  }
  const what = schedule.page
    ? schedule.watchFor || schedule.request
    : schedule.regularUpdates
      ? "Regular updates"
      : "Change alerts";
  const repeat = repeatText(schedule);
  return `${what} · ${repeat.charAt(0).toLowerCase()}${repeat.slice(1)}`;
}

function Section({
  id,
  icon: Icon,
  name,
  count,
  link,
  children,
}: {
  id: string;
  icon: LucideIcon;
  name: string;
  count: number;
  link?: { id: string; name: string; onClick: () => void };
  children: ReactNode;
}) {
  return (
    <section id={id}>
      <div className="box-content flex h-[22px] items-center gap-[7px] px-0.5 pb-1.5 text-[13px] font-semibold">
        <Icon aria-hidden className="size-[15px] text-muted-foreground" />
        {name}
        <span className="font-normal text-muted-foreground">{count}</span>
        {link && (
          <button
            type="button"
            id={link.id}
            onClick={link.onClick}
            className="ml-auto flex items-center gap-[3px] text-xs font-medium text-primary outline-none focus-visible:underline"
          >
            {link.name}
            <ChevronRight aria-hidden className="size-3" />
          </button>
        )}
      </div>
      <div className="overflow-hidden rounded-xl border border-border">{children}</div>
    </section>
  );
}

// A row: the page's icon, or a clock, terminal, or chat; its title over what
// it says; and at the end a time or buttons.
function Row({
  row,
  under,
  onOpen,
  children,
}: {
  row: NewTabRow;
  under: ReactNode;
  onOpen: () => void;
  children: ReactNode;
}) {
  const Icon = row.page ? Globe : row.trigger ? TerminalIcon : row.scheduled ? Clock : AskIcon;
  return (
    <div
      role="button"
      tabIndex={0}
      data-chat={row.chatId}
      onClick={onOpen}
      onKeyDown={(event) => {
        if (event.key === "Enter") {
          onOpen();
        }
      }}
      className="group flex h-[52px] cursor-default items-center gap-[11px] border-t border-border pr-2.5 pl-3 text-[13px] whitespace-nowrap outline-none first:border-t-0 hover:bg-accent focus-visible:bg-accent"
    >
      {row.favicon ? (
        <img src={row.favicon} alt="" className="size-4 shrink-0" />
      ) : (
        <Icon aria-hidden className="size-4 shrink-0 text-muted-foreground" />
      )}
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <b className="truncate font-medium">{row.title || "New chat"}</b>
        <span className="truncate text-[12.5px] text-muted-foreground">{under}</span>
      </div>
      {children}
    </div>
  );
}

// What the row says first, in its color, then the rest.
function Said({ row }: { row: NewTabRow }) {
  return (
    <>
      <span className={cn("font-medium", KIND_COLORS[row.what] ?? "text-foreground")}>
        {row.what}
      </span>
      {row.text && ` · ${row.text}`}
    </>
  );
}

function Time({ children }: { children: ReactNode }) {
  return (
    <span className="mt-[9px] shrink-0 self-start text-xs text-muted-foreground tabular-nums group-focus-within:hidden group-hover:hidden">
      {children}
    </span>
  );
}

const openRow = (row: NewTabRow) =>
  row.page
    ? newTabHandler.openPage(row.chatId, row.alertId)
    : newTabHandler.openChat(row.chatId, row.alertId);

function NeedsYou({ rows }: { rows: NewTabRow[] }) {
  return (
    <Section id="newtab-needs-you" icon={Hand} name="Needs you" count={rows.length}>
      {rows.map((row) => {
        // A run that could not sign in sends the user to the page.
        const signIn = row.what === "Sign in required";
        const onOpen = () =>
          signIn
            ? newTabHandler.openPage(row.chatId, row.alertId)
            : newTabHandler.openChat(row.chatId, row.alertId);
        return (
          <Row key={row.alertId || row.chatId} row={row} under={<Said row={row} />} onOpen={onOpen}>
            <Button
              variant="secondary"
              size="compact"
              tabIndex={-1}
              className="h-[26px] shrink-0 px-2.5"
              onClick={(event) => {
                event.stopPropagation();
                onOpen();
              }}
            >
              {signIn ? "Open page" : "Review"}
            </Button>
          </Row>
        );
      })}
    </Section>
  );
}

function Finished({ rows, count }: { rows: NewTabRow[]; count: number }) {
  const button =
    "grid size-7 shrink-0 place-items-center rounded-[7px] text-muted-foreground outline-none hover:bg-background hover:text-foreground hover:shadow-[0_1px_3px_rgba(30,27,46,.14)] focus-visible:bg-background [&_svg]:size-[15px]";
  return (
    <Section
      id="newtab-finished"
      icon={Flag}
      name="Finished"
      count={count}
      link={{ id: "newtab-inbox", name: "Inbox", onClick: () => newTabHandler.showInbox() }}
    >
      {rows.map((row) => (
        <Row
          key={row.alertId || row.chatId}
          row={row}
          under={<Said row={row} />}
          onOpen={() => openRow(row)}
        >
          <Time>{rowTime(row.time)}</Time>
          <span className="hidden items-center gap-1.5 group-focus-within:flex group-hover:flex">
            {/* A watched page's row opens the page, so its chat is a chip away. */}
            {row.page && (
              <button
                type="button"
                data-open-chat={row.chatId}
                onClick={(event) => {
                  event.stopPropagation();
                  newTabHandler.openChat(row.chatId, row.alertId);
                }}
                className="flex h-[22px] items-center rounded-md bg-background px-2 text-[11.5px] font-medium shadow-[0_1px_3px_rgba(30,27,46,.14)] outline-none focus-visible:ring-2 focus-visible:ring-ring"
              >
                Open chat
              </button>
            )}
            <button
              type="button"
              data-dismiss={row.alertId || row.chatId}
              title="Dismiss"
              aria-label="Dismiss"
              onClick={(event) => {
                event.stopPropagation();
                newTabHandler.dismiss(row.chatId, row.alertId);
              }}
              className={button}
            >
              <X aria-hidden />
            </button>
          </span>
        </Row>
      ))}
    </Section>
  );
}

function ComingUp({ rows, count }: { rows: NewTabRow[]; count: number }) {
  return (
    <Section
      id="newtab-coming-up"
      icon={Clock}
      name="Coming up"
      count={count}
      link={{
        id: "newtab-scheduled",
        name: "Scheduled",
        onClick: () => newTabHandler.showScheduled(),
      }}
    >
      {rows.map((row) => (
        <Row key={row.chatId} row={row} under={comingUpText(row)} onOpen={() => openRow(row)}>
          <span className="mt-[9px] shrink-0 self-start text-xs text-muted-foreground tabular-nums">
            {row.time ? rowTime(row.time) : "Trigger"}
          </span>
        </Row>
      ))}
    </Section>
  );
}

// Under the box, in two columns: what wants the user on the left, what goes
// on by itself on the right. A section with nothing to show is left out.
function Sections({ sections }: { sections: NewTabSections }) {
  const left = sections.needsYou.length > 0 || sections.finished.length > 0;
  const right = sections.comingUp.length > 0;
  if (!left && !right) {
    return null;
  }
  return (
    <div className="mt-7 grid w-full max-w-[760px] grid-cols-2 items-start gap-x-6">
      <div className="flex min-w-0 flex-col gap-[18px]">
        {sections.needsYou.length > 0 && <NeedsYou rows={sections.needsYou} />}
        {sections.finished.length > 0 && (
          <Finished rows={sections.finished} count={sections.finishedCount} />
        )}
      </div>
      <div className="flex min-w-0 flex-col gap-[18px]">
        {right && <ComingUp rows={sections.comingUp} count={sections.comingUpCount} />}
      </div>
    </div>
  );
}

function NewTabPage() {
  const [box, setBox] = useState<NewTabBox | null>(null);
  const [sections, setSections] = useState<NewTabSections | null>(null);
  useEffect(() => {
    const ids = [
      newTabPage.onBox.addListener((next: NewTabBox) => setBox(next)),
      newTabPage.onSections.addListener((next: NewTabSections) => setSections(next)),
    ];
    connect();
    return () => {
      for (const id of ids) {
        newTabPage.removeListener(id);
      }
    };
  }, []);
  if (!box) {
    return null;
  }
  return (
    <main id="newtab" className="flex flex-col items-center px-10 pt-10">
      <picture className="mb-[18px] block size-11">
        <source srcSet="/logo-dark.svg" media="(prefers-color-scheme: dark)" />
        <img alt="uw" src="/logo-light.svg" className="-m-[5.5px] size-[55px] max-w-none" />
      </picture>
      <Box box={box} />
      {box.privateWindow && (
        <p
          id="newtab-private"
          className="mt-3.5 w-full max-w-[760px] px-[18px] text-[13px] text-muted-foreground"
        >
          A private window. uw keeps nothing from it after it closes.
        </p>
      )}
      {sections && <Sections sections={sections} />}
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
      <NewTabPage />
    </MotionConfig>
  </StrictMode>,
);
