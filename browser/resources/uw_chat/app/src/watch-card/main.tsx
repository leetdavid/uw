// The watch card, under the eye in the address bar. It watches the page of
// the tab it opened for, or shows the scheduled chats that watch that page:
// one watch's card, or with several, a list of them. See the frames "Watch
// this page: the card", "Watch this page: every state", "Several watches: the
// eye's card", and "Watch from a chat and several watches: every state" on the
// canvas, and docs/product.md, "Scheduled chats".
import "@/trusted-types";
import "@/theme-colors";
import { StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  connect,
  watchCardHandler,
  watchCardPage,
  type WatchCardState,
} from "./watch-card-api";
import { WatchForm, WatchList, Watched, viewOf } from "./watch-parts";

// What the card shows: the page's watches, one watch, a watch being changed,
// or a new watch of the page.
type Showing =
  | { kind: "watches" }
  | { kind: "watch"; chatId: string }
  | { kind: "edit"; chatId: string }
  | { kind: "new" };

function WatchCard() {
  const [card, setCard] = useState<WatchCardState | null>(null);
  const [showing, setShowing] = useState<Showing>({ kind: "watches" });
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
    document.documentElement.classList.toggle("frosted", !!card?.frosted);
  }, [card]);
  useEffect(() => {
    if (card && !shown.current) {
      shown.current = true;
      watchCardHandler.showUI();
    }
  }, [card]);
  if (!card) {
    return null;
  }
  const watches = card.watches;
  const fresh = viewOf(card, { chatId: "", state: card.fresh, runs: [] });
  const watchOf = (chatId: string) =>
    watches.find((watch) => watch.chatId === chatId);
  const toWatches = () => setShowing({ kind: "watches" });
  const another = () => setShowing({ kind: "new" });
  let body;
  if (watches.length === 0) {
    body = (
      <WatchForm
        card={fresh}
        onWatch={(schedule) => {
          watchCardHandler.watch("", schedule);
          watchCardHandler.close();
        }}
        onCancel={() => watchCardHandler.close()}
      />
    );
  } else if (showing.kind === "new") {
    body = (
      <WatchForm
        key="new"
        card={fresh}
        onBack={toWatches}
        onWatch={(schedule) => {
          watchCardHandler.watch("", schedule);
          toWatches();
        }}
        onCancel={toWatches}
      />
    );
  } else {
    const selected =
      showing.kind === "watch" || showing.kind === "edit"
        ? watchOf(showing.chatId)
        : watches.length === 1
          ? watches[0]
          : undefined;
    // With several watches, a watch's card goes back to their list.
    const back = watches.length > 1 ? toWatches : undefined;
    if (!selected) {
      body = (
        <WatchList
          page={card}
          watches={watches}
          onOpen={(chatId) => setShowing({ kind: "watch", chatId })}
          onAnother={another}
        />
      );
    } else if (showing.kind === "edit") {
      body = (
        <WatchForm
          key={selected.chatId}
          card={viewOf(card, selected)}
          onBack={() => setShowing({ kind: "watch", chatId: selected.chatId })}
          onWatch={(schedule) => {
            watchCardHandler.watch(selected.chatId, schedule);
            watchCardHandler.close();
          }}
          onCancel={() => watchCardHandler.close()}
        />
      );
    } else {
      body = (
        <Watched
          card={viewOf(card, selected)}
          onBack={back}
          onOpenChat={() => watchCardHandler.openChat(selected.chatId)}
          onEdit={() => setShowing({ kind: "edit", chatId: selected.chatId })}
          onCheckNow={() => watchCardHandler.checkNow(selected.chatId)}
          onStopWatching={() => {
            watchCardHandler.stopWatching(selected.chatId);
            if (watches.length > 1) {
              toWatches();
            } else {
              watchCardHandler.close();
            }
          }}
          onAnother={another}
        />
      );
    }
  }
  return (
    <div
      id="watch-card"
      role="dialog"
      aria-label="Watch this page"
      className="flex w-96 flex-col gap-2.5 bg-background p-4"
    >
      {body}
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
