// The watch card, under the eye in the address bar. It watches the page of
// the tab it opened for, or shows the scheduled chat that watches that page.
// See the frames "Watch this page: the card" and "Watch this page: every
// state" on the canvas page "Agent chats", and docs/product.md, "Scheduled
// chats".
import "@/trusted-types";
import "@/theme-colors";
import { StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  ArrowUpRight,
  Check,
  Clock,
  Eye,
  ListPlus,
  Loader2,
  Package,
  Pencil,
  Sparkles,
  Tag,
  TriangleAlert,
  type LucideIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { ChatSchedule } from "../chat-api";
import { ProviderIcon } from "../providers/provider-parts";
import {
  providersHandler,
  type ModelList,
  type Provider,
} from "../providers/providers-api";
import { WatchChart } from "../chart";
import { ScheduleRows, describeSchedule, whenText } from "../schedule";
import {
  connect,
  watchCardHandler,
  watchCardPage,
  type WatchCardState,
} from "./watch-card-api";

type Pick = "price" | "stock" | "new" | "anything";

// The fixed "Watch for" chips. A pick becomes the chat's request, so no model
// fills in the card.
const PICKS: { pick: Pick; label: string; icon: LucideIcon }[] = [
  { pick: "price", label: "Price drops", icon: Tag },
  { pick: "stock", label: "Back in stock", icon: Package },
  { pick: "new", label: "New items", icon: ListPlus },
  { pick: "anything", label: "Anything worth knowing", icon: Sparkles },
];

const PRICE_BELOW = "Price drops below ";

// The page as a request names it, such as "Jarvis Bamboo Standing Desk on
// fully.com".
function pageName(card: WatchCardState): string {
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
function useModels(providerId: string | undefined) {
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

// A watched page's eye: filled, with its pupil cut out.
function FilledEye({ className }: { className?: string }) {
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

function PageLine({ card }: { card: WatchCardState }) {
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

// A new watch, or a watch being changed.
function WatchForm({
  card,
  onSave,
}: {
  card: WatchCardState;
  onSave: () => void;
}) {
  const saved = pickOf(card.watchFor);
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
      ? card.watchFor
      : "";
  return (
    <>
      <h2 className="flex items-center gap-2 text-[15px] font-semibold">
        <Eye aria-hidden className="size-[17px] text-primary" />
        Watch this page
      </h2>
      <PageLine card={card} />
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
          onClick={() => watchCardHandler.close()}
        >
          Cancel
        </Button>
        <Button
          id="watch-save"
          variant="primary"
          size="compact"
          disabled={!request || !draft.model.model}
          onClick={() => {
            watchCardHandler.watch({ ...draft, request }, watchFor);
            onSave();
          }}
        >
          {card.chatId ? "Save" : "Watch"}
        </Button>
      </div>
    </>
  );
}

// How the watch's last run went, or how its run goes now.
function LastRun({ card }: { card: WatchCardState }) {
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
function Watched({
  card,
  onEdit,
}: {
  card: WatchCardState;
  onEdit: () => void;
}) {
  const schedule = card.state.schedule;
  const { providers } = useModels(undefined);
  const provider = providers.find(
    (each) => each.id === schedule.model.providerId,
  );
  return (
    <>
      <h2 className="flex items-center gap-2 text-[15px] font-semibold">
        <FilledEye className="size-[17px] text-primary" />
        Watching this page
      </h2>
      <PageLine card={card} />
      <p
        id="watch-for"
        className="flex items-center gap-2 rounded-[9px] bg-primary/[0.08] px-3 py-2 text-[13px] font-medium text-primary"
      >
        <Tag aria-hidden className="size-3.5 shrink-0" />
        {card.watchFor || schedule.request}
      </p>
      <WatchChart runs={card.runs} watchFor={card.watchFor} />
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
          onClick={() => watchCardHandler.openChat()}
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
          onClick={() => watchCardHandler.stopWatching()}
        >
          Stop watching
        </Button>
      </div>
    </>
  );
}

function WatchCard() {
  const [card, setCard] = useState<WatchCardState | null>(null);
  const [editing, setEditing] = useState(false);
  const shown = useRef(false);
  useEffect(() => {
    const id = watchCardPage.onCard.addListener((next: WatchCardState) =>
      setCard(next),
    );
    connect();
    return () => {
      watchCardPage.removeListener(id);
    };
  }, []);
  useEffect(() => {
    if (card && !shown.current) {
      shown.current = true;
      watchCardHandler.showUI();
    }
  }, [card]);
  if (!card) {
    return null;
  }
  return (
    <div
      id="watch-card"
      role="dialog"
      aria-label="Watch this page"
      className="flex w-96 flex-col gap-2.5 bg-background p-4"
    >
      {card.chatId && !editing ? (
        <Watched card={card} onEdit={() => setEditing(true)} />
      ) : (
        <WatchForm
          key={card.chatId}
          card={card}
          onSave={() => setEditing(false)}
        />
      )}
    </div>
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
      <WatchCard />
    </MotionConfig>
  </StrictMode>,
);
