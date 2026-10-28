// The watch card's parts: the form that starts or changes a watch, and a
// watched page's card. The watch card under the eye and the Scheduled page
// share them. See the frames "Watch this page: the card", "Watch this page:
// every state", and "Scheduled page: watch a page (Fluid)" on the canvas page
// "Agent chats".
import { useEffect, useRef, useState, type ReactNode } from "react";
import {
  ArrowLeft,
  ArrowUpRight,
  Check,
  ChevronRight,
  Clock,
  Eye,
  Loader2,
  MessageCircle,
  Pencil,
  Plus,
  Sparkles,
  Tag,
  TriangleAlert,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { ChatSchedule, ScheduleState, Step } from "../chat-api";
import { ProviderIcon } from "../providers/provider-parts";
import {
  providersHandler,
  type ModelList,
  type Provider,
} from "../providers/providers-api";
import { WatchChart } from "../chart";
import {
  FilledEye,
  ScheduleRows,
  WATCH_CHIPS,
  describeSchedule,
  whenText,
  type WatchPick,
} from "../schedule";
import type { PageWatch } from "./watch-card-api";

// A watch as its card shows it: the page, and the watching chat's schedule and
// runs, or a new watch's settings with no chat.
export interface WatchView {
  title: string;
  host: string;
  favicon: string;
  chatId: string;
  state: ScheduleState;
  runs: Step[];
}

// The page of a card and one of its watches, as a view.
export function viewOf(
  page: { title: string; host: string; favicon: string },
  watch: PageWatch,
): WatchView {
  return {
    title: page.title,
    host: page.host,
    favicon: page.favicon,
    chatId: watch.chatId,
    state: watch.state,
    runs: watch.runs,
  };
}

type Pick = WatchPick;
const PICKS = WATCH_CHIPS;

const PRICE_BELOW = "Price drops below ";

// The page as a request names it, such as "Jarvis Bamboo Standing Desk on
// fully.com".
function pageName(card: WatchView): string {
  const title = card.title.trim();
  if (!title) {
    return card.host;
  }
  return card.host && !title.toLowerCase().includes(card.host.toLowerCase())
    ? `${title} on ${card.host}`
    : title;
}

function requestFor(pick: Pick, page: string, below: string): string {
  switch (pick) {
    case "price":
      return below
        ? `Tell me when the price of ${page} drops below ${below}.`
        : `Tell me when the price of ${page} drops.`;
    case "stock":
      return `Tell me when ${page} is back in stock.`;
    case "new":
      return `Tell me about new items on ${page}.`;
    case "anything":
      return `Tell me about anything worth knowing that changes on ${page}.`;
  }
}

function labelFor(pick: Pick, below: string): string {
  return pick === "price" && below
    ? `${PRICE_BELOW}${below}`
    : PICKS.find((each) => each.pick === pick)!.label;
}

// The chip a saved watch was made with, read back from its label.
function pickOf(watchFor: string): { pick: Pick | null; below: string } {
  if (watchFor.startsWith(PRICE_BELOW)) {
    return { pick: "price", below: watchFor.slice(PRICE_BELOW.length) };
  }
  const found = PICKS.find((each) => each.label === watchFor);
  return { pick: found?.pick ?? null, below: "" };
}

// The providers for the Model row, each listed once; one whose list failed is
// asked again the next time the menu opens.
export function useModels(providerId: string | undefined) {
  const [providers, setProviders] = useState<Provider[]>([]);
  const [lists, setLists] = useState<Record<string, ModelList | undefined>>(
    {},
  );
  const listed = useRef(new Set<string>());
  const listModels = (id: string) => {
    if (listed.current.has(id)) {
      return;
    }
    listed.current.add(id);
    providersHandler.listModels(id).then(({ list }) => {
      if (!list.ok) {
        listed.current.delete(id);
      }
      setLists((before) => ({ ...before, [id]: list }));
    });
  };
  const loadProviders = async () => {
    const { state } = await providersHandler.getState();
    const usable = state.providers.filter((provider) => provider.servesLanguage);
    setProviders(usable);
    return usable;
  };
  useEffect(() => {
    loadProviders();
  }, []);
  useEffect(() => {
    if (providerId) {
      listModels(providerId);
    }
  }, [providerId]);
  return {
    providers,
    lists,
    openModels: async () => {
      for (const provider of await loadProviders()) {
        listModels(provider.id);
      }
    },
  };
}

function Heading({
  eye,
  text,
  onBack,
}: {
  eye: ReactNode;
  text: string;
  onBack?: () => void;
}) {
  return (
    <h2 className="flex items-center gap-2 text-[15px] font-semibold">
      {onBack ? (
        <button
          type="button"
          id="watch-back"
          aria-label="Back"
          className="-m-1 grid size-[25px] place-items-center rounded-[7px] text-muted-foreground outline-none hover:bg-accent hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring"
          onClick={onBack}
        >
          <ArrowLeft aria-hidden className="size-[17px]" />
        </button>
      ) : (
        eye
      )}
      {text}
    </h2>
  );
}

// "Watch for something else", under a hairline at the end of a card.
function Another({ onAnother }: { onAnother: () => void }) {
  return (
    <>
      <div className="mt-1 h-px bg-border" />
      <button
        type="button"
        id="watch-another"
        className="flex items-center gap-2 self-start rounded-[7px] text-[13px] font-medium text-primary outline-none focus-visible:ring-2 focus-visible:ring-ring"
        onClick={onAnother}
      >
        <Plus aria-hidden className="size-3.5" />
        Watch for something else
      </button>
    </>
  );
}

function PageLine({ card }: { card: WatchView }) {
  return (
    <p
      id="watch-page"
      className="-mt-1.5 flex min-w-0 items-center gap-1.5 text-[13px]"
    >
      {card.favicon && (
        <img src={card.favicon} alt="" className="size-4 shrink-0 rounded-[3px]" />
      )}
      <span className="truncate">{card.title || card.host}</span>{" "}
      <span className="shrink-0 text-muted-foreground">· {card.host}</span>
    </p>
  );
}

// A new watch, or a watch being changed. `top` takes the page line's place,
// and `blocked` keeps the watch from being saved.
export function WatchForm({
  card,
  heading = "Watch this page",
  top,
  blocked = false,
  onBack,
  onWatch,
  onCancel,
}: {
  card: WatchView;
  heading?: string;
  top?: ReactNode;
  blocked?: boolean;
  onBack?: () => void;
  onWatch: (schedule: ChatSchedule) => void;
  onCancel: () => void;
}) {
  const saved = pickOf(card.state.schedule.watchFor);
  const [pick, setPick] = useState<Pick | null>(saved.pick);
  const [below, setBelow] = useState(saved.below);
  const [words, setWords] = useState(
    card.chatId && !saved.pick ? card.state.schedule.request : "",
  );
  const [draft, setDraft] = useState<ChatSchedule>(card.state.schedule);
  const { providers, lists, openModels } = useModels(
    draft.model.providerId || undefined,
  );
  const request = pick
    ? requestFor(pick, pageName(card), below.trim())
    : words.trim();
  const watchFor = pick
    ? labelFor(pick, below.trim())
    : card.chatId && request === card.state.schedule.request
      ? card.state.schedule.watchFor
      : "";
  return (
    <>
      <Heading
        eye={<Eye aria-hidden className="size-[17px] text-primary" />}
        text={heading}
        onBack={onBack}
      />
      {top ?? <PageLine card={card} />}
      <p className="mt-1 text-xs font-medium text-muted-foreground">
        Watch for
      </p>
      <div className="-mt-1 flex flex-wrap gap-1.5" role="radiogroup">
        {PICKS.map((each) => (
          <button
            key={each.pick}
            id={`watch-for-${each.pick}`}
            type="button"
            role="radio"
            aria-checked={pick === each.pick}
            className={cn(
              "inline-flex h-[30px] items-center gap-1.5 rounded-[9px] bg-accent px-2.5 text-[13px] font-medium outline-none focus-visible:ring-2 focus-visible:ring-ring",
              pick === each.pick &&
                "bg-primary/[0.08] text-primary ring-1 ring-primary",
            )}
            onClick={() => {
              setPick(each.pick);
              setWords("");
            }}
          >
            <each.icon aria-hidden className="size-3.5" />
            {each.label}
          </button>
        ))}
      </div>
      {pick === "price" && (
        <div className="flex items-center gap-2.5 rounded-[9px] bg-accent py-1.5 pr-2.5 pl-2.5 text-[13px]">
          <label htmlFor="watch-below" className="text-muted-foreground">
            Only below
          </label>
          <input
            id="watch-below"
            className="h-7 w-24 rounded-[7px] bg-background px-2 ring-1 ring-primary outline-none"
            placeholder="$500"
            value={below}
            onChange={(event) => setBelow(event.target.value)}
          />
          <span className="ml-auto text-xs text-muted-foreground">
            Optional
          </span>
        </div>
      )}
      <textarea
        id="watch-words"
        aria-label="What to watch for, in your own words"
        rows={words ? 2 : 1}
        placeholder="Or describe it in your own words…"
        className="resize-none rounded-[9px] bg-accent px-2.5 py-2 text-[13px] outline-none focus-visible:ring-2 focus-visible:ring-primary"
        value={words}
        onChange={(event) => {
          setWords(event.target.value);
          if (event.target.value.trim()) {
            setPick(null);
          }
        }}
      />
      <div className="my-1 h-px bg-border" />
      <ScheduleRows
        draft={draft}
        zoneName={card.state.zoneName}
        providers={providers}
        lists={lists}
        onOpenModels={openModels}
        onChange={(change) => setDraft((before) => ({ ...before, ...change }))}
      />
      <div className="mt-1.5 flex items-center gap-2">
        <span className="mr-auto text-xs text-muted-foreground">
          Runs out of sight while uw is open
        </span>
        <Button
          id="watch-cancel"
          variant="secondary"
          size="compact"
          onClick={onCancel}
        >
          Cancel
        </Button>
        <Button
          id="watch-save"
          variant="primary"
          size="compact"
          disabled={blocked || !request || !draft.model.model}
          onClick={() => onWatch({ ...draft, request, watchFor })}
        >
          {card.chatId ? "Save" : "Watch"}
        </Button>
      </div>
    </>
  );
}

// How the watch's last run went, or how its run goes now.
function LastRun({ card }: { card: WatchView }) {
  const state = card.state;
  if (state.running) {
    return (
      <>
        <Loader2 aria-hidden className="size-3.5 shrink-0 animate-spin" />
        Running now
      </>
    );
  }
  if (state.paused && state.pauseReason) {
    return (
      <>
        <TriangleAlert aria-hidden className="size-3.5 shrink-0 text-warning" />
        {state.pauseReason}
      </>
    );
  }
  if (state.signIn) {
    return (
      <>
        <TriangleAlert aria-hidden className="size-3.5 shrink-0 text-warning" />
        Sign in required on {state.signIn}
      </>
    );
  }
  return (
    <>
      <Check aria-hidden className="size-3.5 shrink-0 text-success" />
      {state.last
        ? `Last run ${whenText(state.last)}${state.lastText ? ` · ${state.lastText}` : ""}`
        : "Not run yet"}
    </>
  );
}

// A watched page's card.
export function Watched({
  card,
  onBack,
  onOpenChat,
  onEdit,
  onStopWatching,
  onAnother,
}: {
  card: WatchView;
  onBack?: () => void;
  onOpenChat: () => void;
  onEdit: () => void;
  onStopWatching: () => void;
  onAnother?: () => void;
}) {
  const schedule = card.state.schedule;
  const { providers } = useModels(undefined);
  const provider = providers.find(
    (each) => each.id === schedule.model.providerId,
  );
  return (
    <>
      <Heading
        eye={<FilledEye className="size-[17px] text-primary" />}
        text="Watching this page"
        onBack={onBack}
      />
      <PageLine card={card} />
      <p
        id="watch-for"
        className="flex items-center gap-2 rounded-[9px] bg-primary/[0.08] px-3 py-2 text-[13px] font-medium text-primary"
      >
        <Tag aria-hidden className="size-3.5 shrink-0" />
        {schedule.watchFor || schedule.request}
      </p>
      <WatchChart runs={card.runs} watchFor={schedule.watchFor} />
      <ul className="flex flex-col gap-1.5 text-[13px] text-muted-foreground">
        <li className="flex items-center gap-2">
          <Clock aria-hidden className="size-3.5 shrink-0" />
          {describeSchedule(schedule)}
          {card.state.paused
            ? " · Paused"
            : card.state.next
              ? ` · Next: ${whenText(card.state.next)}`
              : ""}
        </li>
        <li className="flex items-center gap-2">
          {provider ? (
            <ProviderIcon preset={provider.preset} className="size-3.5" />
          ) : (
            <Sparkles aria-hidden className="size-3.5 shrink-0" />
          )}
          Runs on {schedule.model.model}
        </li>
        <li id="watch-last" className="flex items-center gap-2">
          <LastRun card={card} />
        </li>
      </ul>
      <div className="mt-1.5 flex items-center gap-2">
        <Button
          id="watch-open-chat"
          variant="primary"
          size="compact"
          leadingIcon={ArrowUpRight}
          onClick={onOpenChat}
        >
          Open chat
        </Button>
        <Button
          id="watch-edit"
          variant="secondary"
          size="compact"
          leadingIcon={Pencil}
          onClick={onEdit}
        >
          Edit
        </Button>
        <Button
          id="watch-stop"
          variant="ghost"
          size="compact"
          className="ml-auto text-destructive"
          onClick={onStopWatching}
        >
          Stop watching
        </Button>
      </div>
      {onAnother && <Another onAnother={onAnother} />}
    </>
  );
}

// A page that several chats watch: a row for each, which opens its card.
export function WatchList({
  page,
  watches,
  onOpen,
  onAnother,
}: {
  page: { title: string; host: string; favicon: string };
  watches: PageWatch[];
  onOpen: (chatId: string) => void;
  onAnother: () => void;
}) {
  return (
    <>
      <Heading
        eye={<FilledEye className="size-[17px] text-primary" />}
        text="Watching this page"
      />
      <PageLine card={viewOf(page, watches[0]!)} />
      <p className="text-xs font-semibold text-muted-foreground">
        {watches.length} watches
      </p>
      <ul id="watch-list" className="-mx-2 -mt-1 flex flex-col">
        {watches.map((watch) => {
          const schedule = watch.state.schedule;
          const Icon =
            WATCH_CHIPS.find((chip) =>
              schedule.watchFor.startsWith(chip.label),
            )?.icon ?? MessageCircle;
          const last = watch.state.last
            ? ` · Last: ${whenText(watch.state.last)}${watch.state.lastText ? `, ${watch.state.lastText}` : ""}`
            : "";
          return (
            <li key={watch.chatId}>
              <button
                type="button"
                data-watch={watch.chatId}
                className="flex w-full items-center gap-2.5 rounded-[9px] px-2 py-1.5 text-left outline-none hover:bg-accent focus-visible:bg-accent"
                onClick={() => onOpen(watch.chatId)}
              >
                <span className="grid size-7 shrink-0 place-items-center rounded-[7px] bg-primary/[0.08] text-primary">
                  <Icon aria-hidden className="size-3.5" />
                </span>
                <span className="flex min-w-0 flex-1 flex-col">
                  <span className="truncate text-[13px] font-medium">
                    {schedule.watchFor || schedule.request}
                  </span>
                  <span className="flex min-w-0 items-center gap-1.5 text-xs text-muted-foreground">
                    <Clock aria-hidden className="size-3 shrink-0" />
                    <span className="truncate">
                      {describeSchedule(schedule)}
                      {watch.state.paused ? " · Paused" : last}
                    </span>
                  </span>
                </span>
                <ChevronRight
                  aria-hidden
                  className="size-3.5 shrink-0 text-muted-foreground"
                />
              </button>
            </li>
          );
        })}
      </ul>
      <Another onAnother={onAnother} />
    </>
  );
}

