/// <reference types="trusted-types" />
// The uw://chat page: one chat with uw's agent. Built from Fluid
// Functionalism's parts; see the "(Fluid)" frames on the canvas page
// "Agent chats".
import { Fragment, StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import { Loader2, Pause, Plus, Trash2 } from "lucide-react";
import {
  Banner,
  BannerAction,
  BannerActions,
  BannerTitle,
} from "@/components/ui/banner";
import { ChatMessage } from "@/components/ui/chat-message";
import { InputMessage } from "@/components/ui/input-message";
import {
  Sidebar,
  SidebarContent,
  SidebarGroup,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarInput,
  SidebarInset,
  SidebarMenu,
  SidebarMenuAction,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
  type SidebarMenuButtonProps,
} from "@/components/ui/sidebar";
import { ThinkingIndicator } from "@/components/ui/thinking-indicator";
import {
  ThinkingStep,
  ThinkingSteps,
  ThinkingStepsContent,
} from "@/components/ui/thinking-steps";
import type { IconComponent, IconName } from "@/lib/icon-context";
import { cn } from "@/lib/utils";
import {
  ChatState,
  chatHandler,
  chatPage,
  connect,
  StepKind,
  type ChatSummary,
  type Step,
} from "@/chat-api";

// One step in plain words, as the chat and its tests show it.
function describe(step: Step): string {
  const args = JSON.parse(step.args);
  const result = JSON.parse(step.result);
  if (result.error) {
    return `${step.tool}: ${result.error}`;
  }
  switch (step.tool) {
    case "open":
      return `Opened ${args.url} in tab ${result.tab}`;
    case "navigate":
      return `Went to ${args.url} in tab ${args.tab}`;
    case "read":
      return `Read ${result.title || result.url} in tab ${args.tab}`;
    case "click":
      return `Clicked element ${args.element} in tab ${args.tab}`;
    case "type":
      return `Typed into element ${args.element} in tab ${args.tab}`;
    case "recall":
      return `Looked again at ${result.result?.title || `step ${args.step}`}`;
    default:
      return step.tool;
  }
}

const TOOL_ICONS: Record<string, IconName> = {
  open: "plus",
  navigate: "arrow-right",
  read: "square-library",
  click: "corner-down-right",
  type: "pencil",
  recall: "rotate-ccw",
};

function isStep(line: Step): boolean {
  return line.kind === StepKind.kTool || line.kind === StepKind.kResumed;
}

// Runs of steps share one Thinking Steps block; every other line stands alone.
function group(lines: Step[]): Step[][] {
  const groups: Step[][] = [];
  for (const line of lines) {
    const last = groups.at(-1);
    if (last && isStep(line) && isStep(last[0]!)) {
      last.push(line);
    } else {
      groups.push([line]);
    }
  }
  return groups;
}

function StepLine({ step }: { step: Step }) {
  if (step.kind === StepKind.kResumed) {
    return <ThinkingStep className="step" icon="play" label="Continued" />;
  }
  const failed = Boolean(JSON.parse(step.result).error);
  return (
    <ThinkingStep
      className={failed ? "step error text-destructive" : "step"}
      icon={failed ? "x" : (TOOL_ICONS[step.tool] ?? "dot")}
      label={describe(step)}
    />
  );
}

function Line({
  line,
  last,
  onContinue,
}: {
  line: Step;
  last: boolean;
  onContinue: () => void;
}) {
  switch (line.kind) {
    case StepKind.kUser:
      return (
        <ChatMessage from="user">
          <span className="mine">{line.text}</span>
        </ChatMessage>
      );
    case StepKind.kAnswer:
      return (
        <ChatMessage from="assistant">
          <span className="answer">{line.text}</span>
        </ChatMessage>
      );
    case StepKind.kFailed:
      return (
        <Banner status="error">
          <BannerTitle className="failed">{line.text}</BannerTitle>
        </Banner>
      );
    case StepKind.kStopped:
      return (
        <Banner className="stopped">
          <BannerTitle>Stopped before it finished.</BannerTitle>
          {last && (
            <BannerActions>
              <BannerAction id="continue" variant="primary" onClick={onContinue}>
                Continue
              </BannerAction>
            </BannerActions>
          )}
        </Banner>
      );
    default:
      return null;
  }
}

const Running: IconComponent = ({ className, ...props }) => (
  <Loader2 {...props} className={cn(className, "animate-spin")} />
);

// How a row in the chat list shows its chat's state: a spinner while the
// agent works, a pause sign once it stopped early, a filled dot for an
// answer not yet seen, and a ring for the rest.
function rowState(
  state: ChatState,
): Pick<SidebarMenuButtonProps, "icon" | "status"> {
  switch (state) {
    case ChatState.kRunning:
      return { icon: Running };
    case ChatState.kStopped:
      return { icon: Pause };
    case ChatState.kUnread:
      return { status: "unread" };
    default:
      return { status: "idle" };
  }
}

const STATE_NAMES: Record<ChatState, string> = {
  [ChatState.kDone]: "done",
  [ChatState.kRunning]: "running",
  [ChatState.kStopped]: "stopped",
  [ChatState.kUnread]: "unread",
};

function when(updated: number): string {
  const date = new Date(updated);
  return date.toDateString() === new Date().toDateString()
    ? date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })
    : date.toLocaleDateString([], { month: "short", day: "numeric" });
}

// Every saved chat, newest first. A click goes to the chat's tab or reopens
// it; the browser does that, so the list works the same in every chat tab.
function ChatList({
  chats,
  current,
}: {
  chats: ChatSummary[];
  current: string;
}) {
  const [query, setQuery] = useState("");
  const wanted = query.trim().toLowerCase();
  const shown = chats.filter((chat) =>
    (chat.title || "New chat").toLowerCase().includes(wanted),
  );
  const midnight = new Date().setHours(0, 0, 0, 0);
  const groups = [
    { label: "Today", chats: shown.filter((chat) => chat.updated >= midnight) },
    { label: "Earlier", chats: shown.filter((chat) => chat.updated < midnight) },
  ].filter((group) => group.chats.length);
  return (
    <Sidebar>
      <SidebarHeader>
        <SidebarMenu>
          <SidebarMenuItem>
            <SidebarMenuButton
              id="new-chat"
              icon={Plus}
              onClick={() => chatHandler.newChat()}
            >
              New chat
            </SidebarMenuButton>
          </SidebarMenuItem>
        </SidebarMenu>
        <SidebarInput
          aria-label="Search chats"
          placeholder="Search chats"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
        />
      </SidebarHeader>
      <SidebarContent>
        {groups.map((group) => (
          <SidebarGroup key={group.label}>
            <SidebarGroupLabel>{group.label}</SidebarGroupLabel>
            <SidebarMenu>
              {group.chats.map((chat) => (
                <SidebarMenuItem key={chat.id}>
                  <SidebarMenuButton
                    data-chat={chat.id}
                    data-title={chat.title}
                    data-state={STATE_NAMES[chat.state]}
                    isActive={chat.id === current}
                    onClick={() => {
                      if (chat.id !== current) {
                        chatHandler.openChat(chat.id);
                      }
                    }}
                    {...rowState(chat.state)}
                  >
                    <span className="flex-1 truncate">
                      {chat.title || "New chat"}
                    </span>
                    <span className="text-muted-foreground tabular-nums group-hover/menu-item:opacity-0">
                      {when(chat.updated)}
                    </span>
                  </SidebarMenuButton>
                  <SidebarMenuAction
                    showOnHover
                    data-delete={chat.id}
                    aria-label={`Delete ${chat.title || "this chat"}`}
                    onClick={() => chatHandler.deleteChat(chat.id)}
                  >
                    <Trash2 />
                  </SidebarMenuAction>
                </SidebarMenuItem>
              ))}
            </SidebarMenu>
          </SidebarGroup>
        ))}
      </SidebarContent>
    </Sidebar>
  );
}

function Chat() {
  const [opened, setOpened] = useState(false);
  const [chatId, setChatId] = useState("");
  const [chats, setChats] = useState<ChatSummary[]>([]);
  const [lines, setLines] = useState<Step[]>([]);
  const [working, setWorking] = useState(false);
  const [draft, setDraft] = useState("");
  const end = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const listeners = [
      chatPage.onOpened.addListener(
        (id: string, title: string, saved: Step[]) => {
          // The chat's id in the address lets session restore reopen it.
          if (location.pathname !== `/${id}`) {
            history.replaceState(null, "", `/${id}`);
          }
          if (title) {
            document.title = title;
          }
          setLines(saved);
          setChatId(id);
          setOpened(true);
          // Tells tests, and anyone looking, which chat the page holds.
          document.body.dataset.chatId = id;
        },
      ),
      chatPage.onChats.addListener((saved: ChatSummary[]) => setChats(saved)),
      chatPage.onStep.addListener((step: Step) => {
        setLines((before) => [...before, step]);
        if (step.kind !== StepKind.kTool) {
          setWorking(false);
        }
      }),
    ];
    connect();
    return () => listeners.forEach((id) => chatPage.removeListener(id));
  }, []);

  useEffect(() => {
    end.current?.scrollIntoView({ block: "end" });
  }, [lines, working]);

  const send = (text: string) => {
    const message = text.trim();
    if (!message || working || !opened) {
      return;
    }
    // The first message names the tab.
    if (!lines.some((line) => line.kind === StepKind.kUser)) {
      document.title = message;
    }
    setLines((before) => [
      ...before,
      { kind: StepKind.kUser, tool: "", args: "", result: "", text: message },
    ]);
    setDraft("");
    setWorking(true);
    chatHandler.send(message);
  };

  const resume = () => {
    setLines((before) => [
      ...before,
      { kind: StepKind.kResumed, tool: "", args: "", result: "", text: "" },
    ]);
    setWorking(true);
    chatHandler.resume();
  };

  const groups = group(lines);
  return (
    <SidebarProvider persist={false} width="15rem" className="h-full">
      <ChatList chats={chats} current={chatId} />
      <SidebarInset className="flex h-full flex-col">
      <main className="flex-1 overflow-y-auto">
        <div className="mx-auto flex max-w-[720px] flex-col gap-4 px-7 py-5">
          <Banner status="warning" className="ungated">
            <BannerTitle>
              This chat clicks and types without asking. Try it only on sites
              where a stray click does no harm.
            </BannerTitle>
          </Banner>
          {groups.map((run, index) =>
            isStep(run[0]!) ? (
              <ThinkingSteps key={index} className="w-full">
                <ThinkingStepsContent>
                  {run.map((step, at) => (
                    <StepLine key={at} step={step} />
                  ))}
                </ThinkingStepsContent>
              </ThinkingSteps>
            ) : (
              <Fragment key={index}>
                <Line
                  line={run[0]!}
                  last={index === groups.length - 1 && !working}
                  onContinue={resume}
                />
              </Fragment>
            ),
          )}
          {working && <ThinkingIndicator id="working" />}
          <div ref={end} />
        </div>
      </main>
      <div className="mx-auto w-full max-w-[720px] px-7 pb-4">
        <InputMessage
          value={draft}
          onValueChange={setDraft}
          onSend={send}
          disabled={!opened}
          status={working ? "streaming" : "idle"}
          placeholder={
            lines.length
              ? "Reply to the agent"
              : "Ask the agent to find or do something"
          }
          textareaProps={{ id: "message", "aria-label": "Message" }}
        />
      </div>
      </SidebarInset>
    </SidebarProvider>
  );
}

// Radix's scroll area, under Fluid's sidebar, writes this one style through
// innerHTML. The page's Trusted Types rule refuses HTML strings, so the
// default policy lets this one through and still refuses the rest.
const RADIX_SCROLL_AREA_STYLE =
  "[data-radix-scroll-area-viewport]{scrollbar-width:none;-ms-overflow-style:none;-webkit-overflow-scrolling:touch;}[data-radix-scroll-area-viewport]::-webkit-scrollbar{display:none}";
window.trustedTypes?.createPolicy("default", {
  createHTML: (html: string) => {
    if (html !== RADIX_SCROLL_AREA_STYLE) {
      throw new TypeError("The chat page writes no HTML strings.");
    }
    return html;
  },
});

// Fluid's parts read dark mode from a class on the root element.
const dark = matchMedia("(prefers-color-scheme: dark)");
const followScheme = () =>
  document.documentElement.classList.toggle("dark", dark.matches);
followScheme();
dark.addEventListener("change", followScheme);

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <MotionConfig reducedMotion="user">
      <Chat />
    </MotionConfig>
  </StrictMode>,
);
