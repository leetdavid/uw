// Agent memory on the chat page: the counts under an answer and the card
// that lists them, the mark on a chat kept out of memory, and the composer's
// chip for it. See docs/plans/agent-memory.md and the frames "Agent memory:
// in a chat" and "Agent memory: every state" on the canvas.
import { useState } from "react";
import { ArrowUpRight, Brain, X } from "lucide-react";
import { Tooltip as Hint } from "@/components/ui/tooltip";
import type { IconComponentProps } from "@/lib/icon-context";
import { cn } from "@/lib/utils";
import { CHIP } from "./providers/model-chip";
import { providersHandler } from "./providers/providers-api";
import { ChatMemory, StepKind, type ChatSummary, type Step } from "./chat-api";

export { ChatMemory };

const MEMORY_TOOLS = new Set(["remember", "forget", "search_memory"]);
// The lines that end what the agent did after a message.
const ENDS = new Set([
  StepKind.kAnswer,
  StepKind.kFailed,
  StepKind.kStoppedByUser,
]);

type Remembered = { id: number; memory: string; chat?: string; now?: string };

// What the agent did with memory before one answer.
export type MemoryEvents = {
  saved: Remembered[];
  forgot: Remembered[];
  recalled: Remembered[];
};

function none(): MemoryEvents {
  return { saved: [], forgot: [], recalled: [] };
}

function add(events: MemoryEvents, step: Step) {
  const result = JSON.parse(step.result);
  if (result.kept) {
    events.saved.push(result.kept);
    if (result.replaced) {
      events.forgot.push({ ...result.replaced, now: result.kept.memory });
    }
  } else if (result.forgot) {
    events.forgot.push(result.forgot);
  }
  for (const found of result.memories ?? []) {
    const seen = [...events.saved, ...events.recalled];
    if (!seen.some((memory) => memory.id === found.id)) {
      events.recalled.push(found);
    }
  }
}

// The chat's lines without the memory tools' steps, and what the agent did
// with memory before each answer: those steps show as counts under it.
export function splitMemory(lines: Step[]): {
  shown: Step[];
  events: Map<Step, MemoryEvents>;
} {
  const shown: Step[] = [];
  const events = new Map<Step, MemoryEvents>();
  let pending = none();
  for (const line of lines) {
    if (line.kind === StepKind.kTool && MEMORY_TOOLS.has(line.tool)) {
      add(pending, line);
      continue;
    }
    if (line.kind === StepKind.kUser) {
      pending = none();
    }
    if (ENDS.has(line.kind)) {
      const { saved, forgot, recalled } = pending;
      if (saved.length || forgot.length || recalled.length) {
        events.set(line, pending);
      }
      pending = none();
    }
    shown.push(line);
  }
  return { shown, events };
}

// Lucide's brain-off, which lucide-react does not have yet.
export function BrainOff({ className, strokeWidth = 2 }: IconComponentProps) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
      className={cn("size-4", className)}
    >
      <path d="M12 18v-6m0-7v1.344m5.707-.051A3 3 0 1 0 12 5a3 3 0 0 0-4.124-2.781" />
      <path d="M17.997 5.125a4 4 0 0 1 2.526 5.77m-.879 8.749A4 4 0 0 1 12 18a4 4 0 1 1-7.967-.517M2 2l20 20" />
      <path d="M20 10.536a4 4 0 0 1 1.533 5.34M3.477 10.895a4 4 0 0 1 1.876-5.542" />
      <path d="M6 18a4 4 0 0 1-2-7.464M9 13a4.17 4.17 0 0 0 2.166-1.5a4 4 0 0 0 .138-.196" />
    </svg>
  );
}

function sourceOf(
  memory: Remembered,
  verb: string,
  chatId: string,
  chats: ChatSummary[],
): string {
  if (verb === "Forgot") {
    return memory.now ? `Now: ${memory.now}` : "";
  }
  if (verb === "Saved" || memory.chat === chatId) {
    return "From this chat";
  }
  if (!memory.chat) {
    return "";
  }
  const title = chats.find((chat) => chat.id === memory.chat)?.title;
  return title ? `From “${title}”` : "From a deleted chat";
}

function MemoryCount({
  verb,
  list,
  chatId,
  chats,
}: {
  verb: string;
  list: Remembered[];
  chatId: string;
  chats: ChatSummary[];
}) {
  const [open, setOpen] = useState(false);
  const count = `${verb} ${list.length} ${list.length === 1 ? "memory" : "memories"}`;
  return (
    <div
      className="relative"
      onMouseEnter={() => setOpen(true)}
      onMouseLeave={() => setOpen(false)}
      onFocus={() => setOpen(true)}
      onBlur={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget)) {
          setOpen(false);
        }
      }}
    >
      <button
        type="button"
        className={cn(
          "memory-count -ml-2 inline-flex h-[26px] items-center gap-2 rounded-[7px] px-2 text-[13px] text-muted-foreground",
          open && "bg-accent text-foreground",
        )}
        aria-expanded={open}
      >
        <Brain aria-hidden className="size-[15px]" strokeWidth={1.6} />
        {count}
      </button>
      {open && (
        <div
          role="dialog"
          aria-label={count}
          className="memory-card absolute top-full left-[-8px] z-30 mt-1 w-[330px] overflow-hidden rounded-xl bg-popover text-[13px] leading-[1.4] text-foreground shadow-lg ring-1 ring-border"
        >
          <h3 className="px-3.5 pt-3 pb-1.5 text-xs font-semibold text-muted-foreground">
            {count}
          </h3>
          <ul className="px-1.5 pb-1.5">
            {list.map((memory) => {
              const source = sourceOf(memory, verb, chatId, chats);
              return (
                <li key={memory.id} className="flex gap-2.5 px-2 py-1.5">
                  <i className="mt-[7px] size-[5px] shrink-0 rounded-full bg-muted-foreground" />
                  <span>
                    {verb === "Forgot" ? (
                      <s className="text-muted-foreground">{memory.memory}</s>
                    ) : (
                      memory.memory
                    )}
                    {source && (
                      <span className="block text-xs text-muted-foreground">
                        {source}
                      </span>
                    )}
                  </span>
                </li>
              );
            })}
          </ul>
          <button
            type="button"
            className="memory-manage flex w-full items-center gap-2 border-t border-border px-3.5 py-2.5 text-left font-medium text-muted-foreground hover:text-foreground"
            onClick={() => providersHandler.showAiPage("memory")}
          >
            <ArrowUpRight aria-hidden className="size-3.5" />
            Manage memory
          </button>
        </div>
      )}
    </div>
  );
}

// "Saved 2 memories", "Forgot 1 memory", and "Recalled 3 memories" under an
// answer, each with a card of its memories on hover.
export function MemoryCounts({
  events,
  chatId,
  chats,
}: {
  events: MemoryEvents;
  chatId: string;
  chats: ChatSummary[];
}) {
  const rows: [string, Remembered[]][] = [
    ["Saved", events.saved],
    ["Forgot", events.forgot],
    ["Recalled", events.recalled],
  ];
  return (
    <div className="memory-counts -mt-2 flex flex-col items-start gap-0.5">
      {rows
        .filter(([, list]) => list.length > 0)
        .map(([verb, list]) => (
          <MemoryCount
            key={verb}
            verb={verb}
            list={list}
            chatId={chatId}
            chats={chats}
          />
        ))}
    </div>
  );
}

// The crossed-out brain after the title of a chat that uses no memory.
export function MemoryMark({ memory }: { memory: ChatMemory }) {
  if (memory !== ChatMemory.kOff && memory !== ChatMemory.kPrivate) {
    return null;
  }
  const hint =
    memory === ChatMemory.kPrivate
      ? "Private chats don't use memory"
      : "Memory is off for this chat";
  return (
    <Hint content={hint} side="bottom">
      <span
        id="memory-off"
        role="img"
        aria-label={hint}
        className="shrink-0 text-muted-foreground"
      >
        <BrainOff className="size-[15px]" />
      </span>
    </Hint>
  );
}

// In the composer of a new chat kept out of memory, until its first message.
export function MemoryOffChip({ onUndo }: { onUndo: () => void }) {
  return (
    <button
      type="button"
      id="memory-off-chip"
      title="Use memory in this chat"
      className={cn(CHIP, "h-7 text-muted-foreground")}
      onClick={onUndo}
    >
      <BrainOff className="size-3.5" />
      Memory off
      <X aria-hidden className="size-3" />
    </button>
  );
}
