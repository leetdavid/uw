// A chat's trigger on the chat page: the strip while it runs, its wakes, and
// why it stopped. See docs/product.md, "Scheduled chats".
import { useEffect, useState } from "react";
import { Square, Terminal as TerminalIcon } from "lucide-react";
import { Button } from "@/components/ui/button";
import { chatHandler, type Step, type TriggerState } from "./chat-api";
import { whenText } from "./schedule";

function Command({ command }: { command: string }) {
  return (
    <code className="rounded bg-background/70 px-1 font-mono text-[11.5px] text-foreground">
      {command}
    </code>
  );
}

function age(since: number): string {
  const minutes = Math.max(0, Math.floor((Date.now() - since) / 60_000));
  if (minutes < 1) {
    return "less than a minute";
  }
  return minutes < 60 ? `${minutes} min` : `${Math.floor(minutes / 60)} h`;
}

export function TriggerStrip({ state }: { state: TriggerState }) {
  // The age moves on without a change from the browser.
  const [, tick] = useState(0);
  useEffect(() => {
    const timer = setInterval(() => tick((count) => count + 1), 30_000);
    return () => clearInterval(timer);
  }, []);
  const wakes =
    state.wakes === 0
      ? "no wakes yet"
      : `${state.wakes} ${state.wakes === 1 ? "wake" : "wakes"}`;
  return (
    <div
      id="trigger-strip"
      className="flex min-h-[34px] items-center gap-2 rounded-[10px] bg-accent py-0.5 pr-1 pl-3 text-[12.5px] text-muted-foreground"
    >
      <TerminalIcon aria-hidden className="size-3.5 shrink-0" />
      <span className="min-w-0 flex-1 truncate">
        <b className="font-medium text-foreground">Trigger</b>{" "}
        <Command command={state.command} /> · running {age(state.started)} ·{" "}
        {wakes}
      </span>
      {!state.lasting && (
        <Button
          id="trigger-stop"
          variant="secondary"
          size="compact"
          className="shrink-0"
          leadingIcon={Square}
          onClick={() => chatHandler.stopTrigger()}
        >
          Stop
        </Button>
      )}
    </div>
  );
}

// What woke the chat's agent; its answer follows as the chat's own lines.
export function WakeLine({ line }: { line: Step }) {
  const wake = JSON.parse(line.args);
  const what = wake.ended
    ? wake.code === 0
      ? "ended"
      : `ended with exit code ${wake.code}`
    : `printed ${wake.lines} ${wake.lines === 1 ? "line" : "lines"}`;
  return (
    <div className="wake-line flex min-h-[26px] items-center gap-1.5 text-xs text-muted-foreground">
      <TerminalIcon aria-hidden className="size-3.5 shrink-0" />
      Trigger · {whenText(wake.time)} · <Command command={wake.command} />{" "}
      {what}
    </div>
  );
}

const STOPPED_WHY: Record<string, string> = {
  user: "you stopped it",
  agent: "the agent stopped it",
  day: "after 24 hours. A schedule card can keep it going",
  too_much: "it printed too much",
  paused: "you paused it",
};

export function TriggerStoppedLine({ line }: { line: Step }) {
  const stopped = JSON.parse(line.args);
  return (
    <div className="trigger-stopped flex min-h-[26px] items-center gap-2 text-[12.5px] text-muted-foreground">
      <Square aria-hidden className="size-3.5 shrink-0" />
      <span className="min-w-0 flex-1">
        Stopped the trigger <Command command={stopped.command} /> ·{" "}
        {STOPPED_WHY[stopped.why] ?? "it stopped"}
      </span>
    </div>
  );
}
