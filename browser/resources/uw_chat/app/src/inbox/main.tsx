// The Inbox, in a bubble under its button at the top of the sidebar: every
// notification the profile's scheduled chats sent, newest first. See the
// frames "Inbox: the pop-up (Fluid)" and "Scheduled page and Inbox: every
// state (Fluid)" on the canvas page "Agent chats", the canvas guide
// uw-scheduled-page-and-inbox, and docs/product.md, "Scheduled chats".
import "@/trusted-types";
import "@/theme-colors";
import { StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  ArrowRight,
  Clock,
  Inbox as InboxIcon,
  MessageCircle,
  Terminal as TerminalIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { KIND_COLORS } from "../alert-kinds";
import { connect, inboxHandler, inboxPage, type InboxAlert } from "./inbox-api";

function clockText(ms: number): string {
  return new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
}

// "Today", "Yesterday", or the date, such as "Oct 24".
function dayText(ms: number): string {
  const day = new Date(ms).setHours(0, 0, 0, 0);
  const days = Math.round((new Date().setHours(0, 0, 0, 0) - day) / 86_400_000);
  if (days === 0) {
    return "Today";
  }
  if (days === 1) {
    return "Yesterday";
  }
  return new Date(ms).toLocaleDateString([], { month: "short", day: "numeric" });
}

function groupsOf(alerts: InboxAlert[]): { day: string; alerts: InboxAlert[] }[] {
  const groups: { day: string; alerts: InboxAlert[] }[] = [];
  for (const alert of alerts) {
    const day = dayText(alert.time);
    if (groups.at(-1)?.day === day) {
      groups.at(-1)!.alerts.push(alert);
    } else {
      groups.push({ day, alerts: [alert] });
    }
  }
  return groups;
}

function Alert({ alert }: { alert: InboxAlert }) {
  // A watched page's alert opens the page, so its chat is a chip away.
  const opensPage = !!alert.page && !alert.waiting;
  const Icon = alert.trigger ? TerminalIcon : Clock;
  return (
    <div className="group relative">
      <button
        type="button"
        data-alert={alert.id}
        onClick={() => inboxHandler.openAlert(alert.id)}
        className="flex w-full flex-col gap-0.5 rounded-[9px] px-2.5 py-2 text-left outline-none hover:bg-accent focus-visible:bg-accent"
      >
        <span className="flex w-full items-center gap-2.5">
          {alert.favicon ? (
            <img
              src={alert.favicon}
              alt=""
              className="size-4 shrink-0 rounded-[3px]"
            />
          ) : (
            <Icon aria-hidden className="size-4 shrink-0 text-muted-foreground" />
          )}
          <b className="min-w-0 flex-1 truncate text-[13px] font-medium">
            {alert.chatTitle}
          </b>
          <span
            className={cn(
              "shrink-0 text-xs text-muted-foreground tabular-nums",
              opensPage && "group-hover:invisible group-focus-within:invisible",
            )}
          >
            {clockText(alert.time)}
          </span>
        </span>
        <span className="line-clamp-2 pl-[26px] text-[12.5px] text-muted-foreground">
          <span
            className={cn(
              "font-medium",
              KIND_COLORS[alert.what] ?? "text-foreground",
            )}
          >
            {alert.what}
          </span>
          {alert.text && ` · ${alert.text}`}
        </span>
      </button>
      {opensPage && (
        <button
          type="button"
          data-open-chat={alert.id}
          onClick={() => inboxHandler.openChat(alert.chatId)}
          className="absolute top-[7px] right-2 flex h-[22px] items-center gap-1 rounded-md bg-background px-2 text-xs font-medium opacity-0 shadow-[0_1px_3px_rgba(30,27,46,.14)] outline-none group-focus-within:opacity-100 group-hover:opacity-100 focus-visible:ring-2 focus-visible:ring-ring [&_svg]:size-3"
        >
          <MessageCircle aria-hidden />
          Open chat
        </button>
      )}
    </div>
  );
}

function Inbox() {
  const [alerts, setAlerts] = useState<InboxAlert[] | null>(null);
  const [scheduled, setScheduled] = useState(0);
  const [frosted, setFrosted] = useState(false);
  const shown = useRef(false);
  useEffect(() => {
    const id = inboxPage.onAlerts.addListener(
      (next: InboxAlert[], count: number, glass: boolean) => {
        setAlerts(next);
        setScheduled(count);
        setFrosted(glass);
      },
    );
    connect();
    return () => {
      inboxPage.removeListener(id);
    };
  }, []);
  useEffect(() => {
    document.documentElement.classList.toggle("frosted", frosted);
  }, [frosted]);
  useEffect(() => {
    if (alerts && !shown.current) {
      shown.current = true;
      inboxHandler.showUI();
    }
  }, [alerts]);
  if (!alerts) {
    return null;
  }
  return (
    <div
      id="inbox"
      role="dialog"
      aria-label="Inbox"
      className="flex max-h-[520px] w-[372px] flex-col p-1.5"
    >
      <header className="flex items-center justify-between py-1.5 pr-1 pl-2.5">
        <h1 className="text-[15px] font-semibold">Inbox</h1>
        <Button
          id="inbox-clear"
          variant="ghost"
          size="compact"
          className="text-[12.5px]"
          disabled={alerts.length === 0}
          onClick={() => inboxHandler.clearAll()}
        >
          Clear all
        </Button>
      </header>
      {alerts.length === 0 ? (
        <div
          id="inbox-empty"
          className="flex flex-col items-center px-6 pt-5 pb-6 text-center"
        >
          <span className="mb-2.5 grid size-10 place-items-center rounded-xl bg-accent text-muted-foreground">
            <InboxIcon aria-hidden className="size-[18px]" />
          </span>
          <b className="text-[13.5px] font-semibold">No alerts</b>
          <p className="mt-1 max-w-64 text-[12.5px] text-muted-foreground">
            What your watched pages and scheduled chats tell you shows here.
          </p>
        </div>
      ) : (
        <div className="-mx-1.5 min-h-0 overflow-y-auto px-1.5">
          {groupsOf(alerts).map((group) => (
            <section key={group.day} className="flex flex-col">
              <h2 className="px-2.5 pt-2 pb-1 text-[11.5px] font-semibold text-muted-foreground">
                {group.day}
              </h2>
              {group.alerts.map((alert) => (
                <Alert key={alert.id} alert={alert} />
              ))}
            </section>
          ))}
        </div>
      )}
      <footer className="mt-1 flex items-center justify-between border-t border-border px-2.5 pt-2.5 pb-1.5">
        <button
          type="button"
          id="inbox-see-all"
          onClick={() => inboxHandler.showScheduled()}
          className="flex items-center gap-1.5 rounded-md text-[12.5px] font-medium text-primary outline-none hover:underline focus-visible:ring-2 focus-visible:ring-ring"
        >
          <ArrowRight aria-hidden className="size-3.5" />
          See all scheduled
        </button>
        <span className="text-xs text-muted-foreground">
          {scheduled === 1 ? "1 scheduled chat" : `${scheduled} scheduled chats`}
        </span>
      </footer>
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
      <Inbox />
    </MotionConfig>
  </StrictMode>,
);
