// The Inbox, in a bubble under its button at the top of the sidebar: every
// notification the profile's scheduled chats sent, newest first. See the
// frames "Inbox: the pop-up (Fluid)" and "Scheduled page and Inbox: every
// state (Fluid)" on the canvas page "Agent chats", the canvas guide
// uw-scheduled-page-and-inbox, and docs/product.md, "Scheduled chats".
import "@/trusted-types";
import "@/theme-colors";
import { useEffect, useState } from "react";
import { motion } from "motion/react";
import {
  ArrowRight,
  Clock,
  Inbox as InboxIcon,
  Loader2,
  MessageCircle,
  RefreshCw,
  Terminal as TerminalIcon,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { KIND_COLORS } from "../alert-kinds";
import { FadeOver, FitToContent, SLIDE } from "../fit-to-content";
import { mount, usePopUp } from "../mount";
import { clockText, dateText, daysFromToday } from "../time-text";
import { connect, inboxHandler, inboxPage, type InboxAlert } from "./inbox-api";

// "Today", "Yesterday", or the date, such as "Oct 24".
function dayText(ms: number): string {
  const days = daysFromToday(ms);
  if (days === 0) {
    return "Today";
  }
  if (days === -1) {
    return "Yesterday";
  }
  return dateText(ms);
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
        data-unread={!alert.read || undefined}
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
              "flex shrink-0 items-center gap-1.5 text-xs text-muted-foreground tabular-nums",
              opensPage && "group-hover:invisible group-focus-within:invisible",
            )}
          >
            {clockText(alert.time)}
            {!alert.read && (
              <span
                role="img"
                aria-label="Unread"
                className="size-1.5 rounded-full bg-primary"
              />
            )}
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
        {alert.running && (
          <span className="mt-1 flex h-6 items-center gap-1.5 pl-[26px] text-xs font-medium text-muted-foreground">
            <Loader2 aria-hidden className="size-3.5 shrink-0 animate-spin" />
            Running now
          </span>
        )}
        {/* Room for Retry, which can't sit inside this button. */}
        {alert.retry && <span aria-hidden className="mt-1 block h-6" />}
      </button>
      {opensPage && (
        <button
          type="button"
          data-open-chat={alert.id}
          onClick={() => inboxHandler.openChat(alert.id)}
          className="absolute top-[7px] right-2 flex h-[22px] items-center gap-1 rounded-md bg-background px-2 text-xs font-medium opacity-0 shadow-[0_1px_3px_rgba(30,27,46,.14)] outline-none group-focus-within:opacity-100 group-hover:opacity-100 focus-visible:ring-2 focus-visible:ring-ring [&_svg]:size-3"
        >
          <MessageCircle aria-hidden />
          Open chat
        </button>
      )}
      {alert.retry && (
        <Button
          variant="ghost"
          tone="primary"
          size="compact"
          leadingIcon={RefreshCw}
          data-retry={alert.id}
          className="absolute bottom-2 left-7 h-6"
          onClick={() => inboxHandler.retry(alert.id)}
        >
          Retry
        </Button>
      )}
    </div>
  );
}

function Inbox() {
  const [alerts, setAlerts] = useState<InboxAlert[] | null>(null);
  const [scheduled, setScheduled] = useState(0);
  const [retryAll, setRetryAll] = useState(0);
  const [frosted, setFrosted] = useState(false);
  useEffect(() => {
    const id = inboxPage.onAlerts.addListener(
      (
        next: InboxAlert[],
        count: number,
        retrying: number,
        glass: boolean,
      ) => {
        setAlerts(next);
        setScheduled(count);
        setRetryAll(retrying);
        setFrosted(glass);
      },
    );
    connect();
    return () => {
      inboxPage.removeListener(id);
    };
  }, []);
  usePopUp(alerts !== null, frosted, () => inboxHandler.showUI());
  if (!alerts) {
    return null;
  }
  return (
    <FitToContent>
      <div
        id="inbox"
        role="dialog"
        aria-label="Inbox"
        className="flex max-h-[520px] w-[372px] flex-col p-1.5"
      >
        <header className="flex items-center justify-between py-1.5 pr-1 pl-2.5">
          <h1 className="mr-auto text-[15px] font-semibold">Inbox</h1>
          {retryAll > 0 && (
            <Button
              id="inbox-retry-all"
              variant="ghost"
              tone="primary"
              size="compact"
              leadingIcon={RefreshCw}
              title={
                retryAll === 1
                  ? "Run the failed scheduled chat again"
                  : `Run the ${retryAll} failed scheduled chats again`
              }
              className="text-[12.5px]"
              onClick={() => inboxHandler.retryAll()}
            >
              Retry all ({retryAll})
            </Button>
          )}
          <Button
            id="inbox-mark-read"
            variant="ghost"
            size="compact"
            className="text-[12.5px]"
            disabled={alerts.every((alert) => alert.read)}
            onClick={() => inboxHandler.markAllRead()}
          >
            Mark all read
          </Button>
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
        <FadeOver view={alerts.length === 0 ? "empty" : "alerts"}>
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
        </FadeOver>
        {/* It moves with the pop-up's bottom edge as the list changes. */}
        <motion.footer
          layout="position"
          transition={SLIDE}
          className="mt-1 flex items-center justify-between border-t border-border px-2.5 pt-2.5 pb-1.5"
        >
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
        </motion.footer>
      </div>
    </FitToContent>
  );
}

mount(<Inbox />);
