// The steps and approvals of the tab tools, which list, show, move, and close
// the user's own tabs. See docs/plans/tab-tools.md and the frames "Tab tools:
// a close waits for you (Fluid)" and "Tab tools: every state (Fluid)".
import { useState, type ReactNode } from "react";
import {
  ArrowRight,
  ChevronDown,
  ChevronRight,
  Folder,
  Info,
  Layers,
  Lock,
} from "lucide-react";
import { ThinkingStep } from "@/components/ui/thinking-steps";
import type { IconName } from "@/lib/icon-context";
import { chatHandler, type Approval, type Step } from "@/chat-api";

const TAB_ICONS: Record<string, IconName> = {
  tabs: "layers",
  select: "arrow-right",
  move: "folder",
  rename_task: "pencil",
  close: "x",
  close_task: "x",
};

export const isTabTool = (tool: string) => tool in TAB_ICONS;

type TabValue = { id: string; title: string; url: string };
type TaskValue = { id: string; name: string; color: string };

// Chromium's tab group colors, as the sidebar draws a task's folder.
const TASK_COLORS: Record<string, string> = {
  grey: "#5f6368",
  blue: "#1a73e8",
  red: "#d93025",
  yellow: "#f9ab00",
  green: "#188038",
  pink: "#d01884",
  purple: "#9334e6",
  cyan: "#007b83",
  orange: "#fa903e",
};

const VERBS: Record<string, string> = {
  select: "show",
  move: "move",
  rename_task: "rename",
  close: "close",
  close_task: "close",
};

const CHIP =
  "tab-chip inline-flex h-[22px] max-w-[240px] items-center gap-1.5 rounded-md bg-accent px-2 align-middle text-xs font-medium text-muted-foreground";

function favicon(url: string): string {
  return `chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${encodeURIComponent(url)}`;
}

// What a tab's address says of it: its site, "terminal", or a uw page.
function site(url: string): string {
  try {
    const { protocol, hostname } = new URL(url);
    if (protocol === "chrome:") {
      return hostname === "terminal" ? "terminal" : `uw://${hostname}`;
    }
    return hostname.replace(/^www\./, "");
  } catch {
    return url;
  }
}

const count = (n: number, one = "tab", many = "tabs") =>
  `${n} ${n === 1 ? one : many}`;

function TabChip({ tab }: { tab: TabValue }) {
  return (
    <button
      type="button"
      className={`${CHIP} hover:text-foreground`}
      onClick={() => chatHandler.showUserTab(tab.id)}
    >
      <img src={favicon(tab.url)} alt="" className="size-3 shrink-0" />
      <span className="truncate">{tab.title || tab.url}</span>
    </button>
  );
}

function TaskChip({ task }: { task: TaskValue }) {
  return (
    <button
      type="button"
      className={`${CHIP} task-chip hover:text-foreground`}
      onClick={() => chatHandler.showUserTask(task.id)}
    >
      <Folder
        aria-hidden
        className="size-3 shrink-0"
        style={{ color: TASK_COLORS[task.color] ?? TASK_COLORS.grey }}
      />
      <span className="truncate">{task.name}</span>
    </button>
  );
}

export function declinedTabStep(step: Step): boolean {
  return (
    isTabTool(step.tool) &&
    String(JSON.parse(step.result).error ?? "").startsWith(
      "The user did not approve",
    )
  );
}

// One tab step in plain words. Null for an error the chat shows as it shows
// any other.
export function describeTabStep(step: Step): ReactNode {
  const args = JSON.parse(step.args);
  const result = JSON.parse(step.result);
  if (result.refused) {
    return (
      <>
        Didn't {VERBS[step.tool]}
        {result.tab && (
          <>
            {" "}
            <span className={CHIP}>{result.tab}</span>
          </>
        )}
        : {result.refused}
      </>
    );
  }
  if (declinedTabStep(step)) {
    const tabs = Array.isArray(args.tabs) ? args.tabs.length : 0;
    return step.tool === "close_task" || step.tool === "rename_task"
      ? `Didn't ${VERBS[step.tool]} the task`
      : `Didn't ${VERBS[step.tool]} ${count(tabs)}`;
  }
  if (result.error) {
    return null;
  }
  const moved = (result.moved ?? result.closed ?? []).length;
  switch (step.tool) {
    case "tabs":
      return (
        <>
          Listed{" "}
          <span className={CHIP}>
            <Layers aria-hidden className="size-3 shrink-0" />
            {count(result.tabs)} in{" "}
            {count(result.workspaces, "workspace", "workspaces")}
          </span>
        </>
      );
    case "select":
      return (
        <>
          Switched to <TabChip tab={result.tab} />
        </>
      );
    case "move":
      return result.scratchpad ? (
        `Moved ${count(moved)} to the Scratchpad`
      ) : (
        <>
          Moved {count(moved)} to {result.new && "a new task, "}
          <TaskChip task={result.task} />
        </>
      );
    case "rename_task":
      return (
        <>
          Renamed <span className={CHIP}>{result.was}</span> to{" "}
          <TaskChip task={result.task} />
        </>
      );
    case "close":
      return result.task ? (
        <>
          Closed {count(moved)} in <TaskChip task={result.task} />
        </>
      ) : (
        `Closed ${count(moved)}`
      );
    default:
      return (
        <>
          Closed the task <TaskChip task={result.task} /> and its {count(moved)}
        </>
      );
  }
}

// A tab step's line, which opens to the tabs it moved or closed, or to the
// list as the model got it.
export function TabStepLine({
  step,
  last,
  children,
}: {
  step: Step;
  last: boolean;
  children: ReactNode;
}) {
  const [open, setOpen] = useState(false);
  const result = JSON.parse(step.result);
  const tabs: TabValue[] = result.closed ?? result.moved ?? [];
  const list: string | undefined = step.tool === "tabs" ? result.list : undefined;
  const opens = !result.error && (tabs.length > 0 || Boolean(list));
  const Chevron = open ? ChevronDown : ChevronRight;
  return (
    <ThinkingStep
      className={result.refused ? "step refused" : "step tab-step"}
      icon={result.refused ? "ban" : TAB_ICONS[step.tool]}
      isLast={last}
      label={
        <span className="inline-flex items-center gap-1">
          {children}
          {opens && (
            <button
              type="button"
              aria-expanded={open}
              aria-label={open ? "Hide the tabs" : "Show the tabs"}
              className="tab-step-toggle grid size-5 place-items-center rounded text-muted-foreground hover:text-foreground"
              onClick={() => setOpen(!open)}
            >
              <Chevron aria-hidden className="size-3.5" />
            </button>
          )}
        </span>
      }
    >
      {open && list && (
        <pre className="tab-list mt-1.5 max-w-full overflow-x-auto rounded-lg bg-surface-1 px-2.5 py-1.5 font-mono text-[11.5px] leading-[1.6] text-muted-foreground">
          {list}
        </pre>
      )}
      {open && tabs.length > 0 && (
        <ul className="tab-titles mt-1 ml-1 border-l border-border pl-3">
          {tabs.map((tab) => (
            <li
              key={tab.id}
              className="flex h-[22px] items-center gap-2 text-xs text-muted-foreground"
            >
              <img src={favicon(tab.url)} alt="" className="size-3 shrink-0" />
              <span className="truncate">{tab.title || tab.url}</span>
            </li>
          ))}
        </ul>
      )}
    </ThinkingStep>
  );
}

// The label of an approval card for a change to the user's tabs.
export function TabApprovalLabel({ approval }: { approval: Approval }) {
  return (
    <span className="flex items-center gap-1.5">
      <Layers aria-hidden className="size-3.5" />
      <span className="font-medium">Your tabs</span>·{" "}
      {[approval.workspaceIcon, approval.workspace].filter(Boolean).join(" ")}
    </span>
  );
}

// What the card shows: the tabs, at most 5, or a rename's old and new name,
// then why it asks.
export function TabApproval({ approval }: { approval: Approval }) {
  return (
    <div className="flex flex-col gap-2">
      {approval.newName ? (
        <div className="approval-rename flex items-center gap-2 rounded-lg bg-surface-1 px-3 py-2 text-[13px]">
          <Folder aria-hidden className="size-3.5 shrink-0 text-muted-foreground" />
          <s className="text-muted-foreground">{approval.task}</s>
          <ArrowRight aria-hidden className="size-3.5 shrink-0 text-muted-foreground" />
          <b className="font-semibold">{approval.newName}</b>
          <Lock aria-hidden className="ml-auto size-3 shrink-0 text-muted-foreground" />
        </div>
      ) : (
        <ul className="approval-tabs rounded-lg bg-surface-1 py-1 text-[12.5px]">
          {approval.tabs.slice(0, 5).map((tab, at) => (
            <li
              key={at}
              className="grid h-7 grid-cols-[14px_minmax(0,1fr)_auto] items-center gap-2.5 px-3"
            >
              <img src={favicon(tab.url)} alt="" className="size-3.5" />
              <span className="truncate">{tab.title || tab.url}</span>
              <span className="text-muted-foreground">{site(tab.url)}</span>
            </li>
          ))}
          {approval.tabs.length > 5 && (
            <li className="flex h-7 items-center pr-3 pl-[34px] text-muted-foreground">
              and {approval.tabs.length - 5} more
            </li>
          )}
        </ul>
      )}
      <p className="approval-reason flex gap-1.5 text-[12.5px] text-muted-foreground">
        <Info aria-hidden className="mt-0.5 size-3.5 shrink-0" />
        {approval.reason}
      </p>
    </div>
  );
}
