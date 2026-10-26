// uw://ai/memory: what the agent knows about the user, one sentence each,
// to search, reword, and forget, with the switch that pauses memory. See the
// frames "Agent memory: the memory page" and "Agent memory: every state" and
// docs/plans/agent-memory.md.
import { Fragment, useEffect, useState, type ReactNode } from "react";
import { Brain, Pencil, Search, Trash2, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Switch } from "@/components/ui/switch";
import { Tooltip } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";
import { BrainOff } from "../brain-off";
import { providersHandler, type MemoryInfo } from "./providers-api";

function memoriesText(count: number): string {
  return `${count} ${count === 1 ? "memory" : "memories"}`;
}

// When a memory last changed: the time today, or the day.
function whenText(ms: number): string {
  const when = new Date(ms);
  const today = new Date().toDateString() === when.toDateString();
  return today
    ? `Today, ${when.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}`
    : when.toLocaleDateString([], { month: "short", day: "numeric" });
}

// `text` with each of `words` marked where it appears.
function marked(text: string, words: string[]): ReactNode {
  if (!words.length) {
    return text;
  }
  const pattern = new RegExp(
    `(${words.map((word) => word.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("|")})`,
    "gi",
  );
  return text.split(pattern).map((part, at) =>
    at % 2 ? (
      <mark key={at} className="rounded-[3px] bg-primary/15 px-px text-inherit">
        {part}
      </mark>
    ) : (
      <Fragment key={at}>{part}</Fragment>
    ),
  );
}

function Source({ memory }: { memory: MemoryInfo }) {
  if (!memory.chat) {
    return null;
  }
  return memory.chatTitle ? (
    <>
      From{" "}
      <button
        type="button"
        data-memory-chat={memory.chat}
        className="text-foreground/80 underline decoration-border underline-offset-2 hover:text-foreground"
        onClick={() => providersHandler.openChat(memory.chat)}
      >
        {memory.chatTitle}
      </button>
    </>
  ) : (
    "From a deleted chat"
  );
}

function MemoryRow({
  memory,
  words,
  onChanged,
}: {
  memory: MemoryInfo;
  words: string[];
  onChanged: () => void;
}) {
  const [draft, setDraft] = useState<string | null>(null);
  const [error, setError] = useState("");
  // The sentence as last saved here, until the list is read again.
  const [text, setText] = useState(memory.text);
  const save = async () => {
    const { error } = await providersHandler.editMemory(memory.id, draft ?? "");
    setError(error);
    if (!error) {
      setText((draft ?? "").trim());
      setDraft(null);
      onChanged();
    }
  };
  if (draft !== null) {
    return (
      <div className="memory-row border-t border-border px-4 py-2.5 first:border-t-0">
        <input
          id="memory-draft"
          aria-label="Memory"
          autoFocus
          value={draft}
          maxLength={300}
          onChange={(event) => setDraft(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") {
              save();
            } else if (event.key === "Escape") {
              setDraft(null);
            }
          }}
          className="h-9 w-full rounded-lg border-[1.5px] border-primary bg-background px-2.5 text-[13.5px] outline-none ring-[3px] ring-primary/15"
        />
        <div className="mt-2 flex items-center gap-2">
          <span
            className={cn(
              "mr-auto truncate text-xs",
              error ? "text-destructive" : "text-muted-foreground",
            )}
          >
            {error || `Was: ${text}`}
          </span>
          <Button variant="ghost" size="compact" onClick={() => setDraft(null)}>
            Cancel
          </Button>
          <Button id="save-memory" size="compact" onClick={save}>
            Save
          </Button>
        </div>
      </div>
    );
  }
  return (
    <div
      data-memory={memory.id}
      className="memory-row group flex items-center gap-4 border-t border-border py-2 pr-2.5 pl-4 first:border-t-0 hover:bg-hover/40"
    >
      <div className="min-w-0 flex-1">
        <p className="memory-text text-[13.5px]">{marked(text, words)}</p>
        <p className="truncate text-xs text-muted-foreground">
          <Source memory={memory} />
          {memory.chat && " · "}
          {whenText(memory.changed)}
        </p>
      </div>
      <div className="flex gap-0.5 opacity-0 group-focus-within:opacity-100 group-hover:opacity-100">
        <Tooltip content="Reword">
          <Button
            variant="ghost"
            size="icon-compact"
            aria-label="Reword"
            data-reword={memory.id}
            onClick={() => setDraft(text)}
          >
            <Pencil />
          </Button>
        </Tooltip>
        <Tooltip content="Forget">
          <Button
            variant="ghost"
            size="icon-compact"
            aria-label="Forget"
            data-forget={memory.id}
            className="hover:text-destructive"
            onClick={() =>
              providersHandler.forgetMemory(memory.id).then(onChanged)
            }
          >
            <Trash2 />
          </Button>
        </Tooltip>
      </div>
    </div>
  );
}

// `onCount` hears how many memories there are, for the section list.
export function MemorySection({ onCount }: { onCount: (count: number) => void }) {
  const [memories, setMemories] = useState<MemoryInfo[] | null>(null);
  const [total, setTotal] = useState(0);
  const [on, setOn] = useState(true);
  const [query, setQuery] = useState("");
  const [confirming, setConfirming] = useState(false);
  const [changes, setChanges] = useState(0);
  const changed = () => setChanges((count) => count + 1);

  useEffect(() => {
    let live = true;
    providersHandler.getMemories(query).then((answer) => {
      if (live) {
        setMemories(answer.memories);
        setTotal(answer.total);
        setOn(answer.on);
        onCount(answer.total);
      }
    });
    return () => {
      live = false;
    };
  }, [query, changes, onCount]);

  // A chat that keeps or forgets a memory, or a pause from elsewhere, shows
  // here at once.
  useEffect(() => {
    let live = true;
    const wait = () =>
      providersHandler.waitForMemoryChange().then(() => {
        if (live) {
          changed();
          wait();
        }
      });
    wait();
    return () => {
      live = false;
    };
  }, []);

  if (!memories) {
    return null;
  }
  const words = query.trim() ? query.trim().split(/\s+/) : [];
  return (
    <div className="flex flex-col gap-4">
      <div className="flex items-center gap-2.5">
        <h1 className="text-[20px] font-semibold tracking-[-0.01em]">Memory</h1>
        <span id="memory-count" className="pt-0.5 text-muted-foreground">
          {memoriesText(total)}
        </span>
        {total > 0 && (
          <Button
            id="forget-all-memories"
            variant="secondary"
            size="compact"
            className="ml-auto text-destructive"
            onClick={() => setConfirming(true)}
          >
            Forget all memories
          </Button>
        )}
      </div>
      <p className="max-w-[660px] text-[13.5px] text-muted-foreground">
        What the agent knows about you, one sentence each, for this profile
        only. It keeps and forgets them as it works. Say “remember that…” or
        “forget that…” in a chat. They stay on this Mac.
      </p>
      <div className="flex items-center gap-3 rounded-xl border border-border px-4 py-2.5">
        <span className="grid size-8 shrink-0 place-items-center rounded-lg bg-accent text-muted-foreground">
          {on ? <Brain className="size-4" /> : <BrainOff className="size-4" />}
        </span>
        <div className="min-w-0 flex-1">
          <p className="text-[13.5px] font-semibold">Use memory</p>
          <p id="memory-use-note" className="text-xs text-muted-foreground">
            {on
              ? "In chats, in the uw command, and in scheduled chats. Off pauses it: nothing is recalled or kept, and nothing here is deleted."
              : `Paused. Nothing is recalled or kept until you turn it back on.${total ? ` The ${memoriesText(total)} ${total === 1 ? "stays" : "stay"}.` : ""}`}
          </p>
        </div>
        <Switch
          id="use-memory"
          label=""
          aria-label="Use memory"
          checked={on}
          onToggle={() => {
            providersHandler.setMemoryOn(!on);
            setOn(!on);
          }}
        />
      </div>
      {total > 0 && (
        <div className="flex items-center gap-2.5">
          <label
            className={cn(
              "flex h-9 w-80 items-center gap-2.5 rounded-lg bg-accent px-2.5 text-muted-foreground",
              query && "bg-background ring-[1.5px] ring-primary",
            )}
          >
            <Search aria-hidden className="size-4 shrink-0" />
            <input
              id="memory-search"
              aria-label="Search memories"
              placeholder="Search memories"
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              className="min-w-0 flex-1 bg-transparent text-foreground outline-none placeholder:text-muted-foreground"
            />
            {query && (
              <button
                type="button"
                aria-label="Clear search"
                onClick={() => setQuery("")}
              >
                <X aria-hidden className="size-3.5" />
              </button>
            )}
          </label>
          <span className="ml-auto text-xs text-muted-foreground">
            {query ? `${memories.length} of ${total}` : "Newest first"}
          </span>
        </div>
      )}
      {total === 0 ? (
        <div
          id="no-memories"
          className="flex flex-col items-center gap-1 rounded-xl border border-border px-6 py-12 text-center"
        >
          <span className="mb-2 grid size-10 place-items-center rounded-[10px] bg-accent text-muted-foreground">
            <Brain className="size-5" />
          </span>
          <p className="text-sm font-semibold">No memories yet</p>
          <p className="max-w-[300px] text-[12.5px] text-muted-foreground">
            The agent keeps what it learns about you as it works. You can also
            say “remember that…” in a chat.
          </p>
        </div>
      ) : memories.length === 0 ? (
        <p id="no-memory-found" className="py-10 text-center text-muted-foreground">
          No memory mentions “{query.trim()}”.
        </p>
      ) : (
        <div id="memories" className="overflow-hidden rounded-xl border border-border">
          {memories.map((memory) => (
            <MemoryRow
              key={`${memory.id}:${memory.text}`}
              memory={memory}
              words={words}
              onChanged={changed}
            />
          ))}
        </div>
      )}
      <Dialog open={confirming} onOpenChange={setConfirming}>
        <DialogContent size="sm">
          <DialogHeader>
            <DialogTitle>Forget all {memoriesText(total)}?</DialogTitle>
            <DialogDescription>
              The agent starts over, knowing nothing about you in this profile.
              Your chats stay. This can't be undone.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="ghost" onClick={() => setConfirming(false)}>
              Cancel
            </Button>
            <button
              type="button"
              id="confirm-forget-all"
              className="h-9 rounded-lg bg-destructive px-4 font-medium text-white"
              onClick={() =>
                providersHandler.forgetAllMemories().then(() => {
                  setConfirming(false);
                  changed();
                })
              }
            >
              Forget all
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
