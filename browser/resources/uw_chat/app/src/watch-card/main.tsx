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
  connect,
  watchCardHandler,
  watchCardPage,
  type WatchCardState,
} from "./watch-card-api";
import { WatchForm, Watched } from "./watch-parts";

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
    document.documentElement.classList.toggle('frosted', !!card?.frosted);
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
  return (
    <div
      id="watch-card"
      role="dialog"
      aria-label="Watch this page"
      className="flex w-96 flex-col gap-2.5 bg-background p-4"
    >
      {card.chatId && !editing ? (
        <Watched
          card={card}
          onOpenChat={() => watchCardHandler.openChat()}
          onEdit={() => setEditing(true)}
          onStopWatching={() => watchCardHandler.stopWatching()}
        />
      ) : (
        <WatchForm
          key={card.chatId}
          card={card}
          onWatch={(schedule) => {
            watchCardHandler.watch(schedule);
            setEditing(false);
          }}
          onCancel={() => watchCardHandler.close()}
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
