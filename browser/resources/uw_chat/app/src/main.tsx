/// <reference types="trusted-types" />
// The uw://chat page: one chat with uw's agent. Built from Fluid
// Functionalism's parts; see the "(Fluid)" frames on the canvas page
// "Agent chats".
import { Fragment, StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import { Columns2, Loader2, Pause, Plus, Sparkles, Trash2 } from "lucide-react";
import {
  Banner,
  BannerAction,
  BannerActions,
  BannerTitle,
} from "@/components/ui/banner";
import {
  AskUserQuestions,
  type AskUserAnswer,
} from "@/components/ui/ask-user-questions";
import { Button } from "@/components/ui/button";
import { ChatMessage } from "@/components/ui/chat-message";
import { InputMessage } from "@/components/ui/input-message";
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
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
import { Switch } from "@/components/ui/switch";
import {
  ThinkingStep,
  ThinkingSteps,
  ThinkingStepsContent,
} from "@/components/ui/thinking-steps";
import type { IconComponent, IconName } from "@/lib/icon-context";
import { cn } from "@/lib/utils";
import {
  ApprovalChoice,
  ChatState,
  chatHandler,
  chatPage,
  connect,
  SkillSource,
  StepKind,
  type AgentTab,
  type Approval,
  type ChatSummary,
  type SkillInfo,
  type SkillProposal,
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
    case "skill":
      return args.file
        ? `Read ${args.file} of the ${args.name} skill`
        : `Used the ${args.name} skill`;
    case "save_skill":
      return result.saved
        ? `Saved the ${args.name} skill`
        : `Did not save the ${args.name} skill`;
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
  skill: "lightbulb",
  save_skill: "check",
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

const Waiting: IconComponent = ({ className }) => (
  <span className={cn(className, "flex items-center justify-center")}>
    <span className="size-1.5 rounded-full bg-warning" />
  </span>
);

// How a row in the chat list shows its chat's state: a spinner while the
// agent works, an amber dot while a step waits for approval, a pause sign
// once it stopped early, a filled dot for an answer not yet seen, and a ring
// for the rest.
function rowState(
  state: ChatState,
): Pick<SidebarMenuButtonProps, "icon" | "status"> {
  switch (state) {
    case ChatState.kRunning:
      return { icon: Running };
    case ChatState.kWaiting:
      return { icon: Waiting };
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
  [ChatState.kWaiting]: "waiting",
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
  view,
  skillsOn,
  onView,
}: {
  chats: ChatSummary[];
  current: string;
  view: View;
  skillsOn: number;
  onView: (view: View) => void;
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
                    isActive={chat.id === current && view === "chat"}
                    onClick={() => {
                      if (chat.id === current) {
                        onView("chat");
                      } else {
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
      <SidebarFooter>
        <SidebarMenu>
          <SidebarMenuItem>
            <SidebarMenuButton
              id="skills"
              icon={Sparkles}
              isActive={view === "skills"}
              onClick={() => onView(view === "skills" ? "chat" : "skills")}
            >
              <span className="flex-1">Skills</span>
              <span className="text-muted-foreground tabular-nums">
                {skillsOn} on
              </span>
            </SidebarMenuButton>
          </SidebarMenuItem>
        </SidebarMenu>
      </SidebarFooter>
    </Sidebar>
  );
}

type View = "chat" | "skills";

const SOURCES: { source: SkillSource | null; label: string }[] = [
  { source: null, label: "All" },
  { source: SkillSource.kUw, label: "uw" },
  { source: SkillSource.kAgents, label: "~/.agents/skills" },
  { source: SkillSource.kChat, label: "Made in chats" },
];

const SOURCE_NAMES: Record<SkillSource, string> = {
  [SkillSource.kUw]: "uw",
  [SkillSource.kAgents]: "~/.agents/skills",
  [SkillSource.kChat]: "Made in a chat",
};

// Every skill the agent can use, each with a switch. Turning one off keeps
// it from every chat until it is turned on again.
function SkillsView({
  skills,
  folder,
  onToggle,
}: {
  skills: SkillInfo[];
  folder: string;
  onToggle: (skill: SkillInfo) => void;
}) {
  const [query, setQuery] = useState("");
  const [source, setSource] = useState<SkillSource | null>(null);
  const wanted = query.trim().toLowerCase();
  const shown = skills.filter(
    (skill) =>
      (source === null || skill.source === source) &&
      `${skill.name} ${skill.description}`.toLowerCase().includes(wanted),
  );
  const on = skills.filter((skill) => skill.on).length;
  return (
    <main className="flex-1 overflow-y-auto">
      <div className="mx-auto flex max-w-[860px] flex-col gap-4 px-8 py-5">
        <div className="flex items-baseline gap-2.5">
          <h1 className="text-base font-semibold">Skills</h1>
          <span className="text-muted-foreground">
            {on} on, {skills.length - on} off
          </span>
        </div>
        <p className="max-w-[640px] text-muted-foreground">
          The agent sees each skill's one-line description and reads a
          skill's instructions only when a request calls for it. Scripts in a
          skill do not run. Add a skill by putting its folder in {folder}, or
          ask a chat to make one.
        </p>
        <div className="flex flex-wrap items-center gap-2.5">
          <input
            aria-label="Search skills"
            placeholder="Search skills"
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            className="h-9 w-64 rounded-lg bg-accent px-3 outline-none placeholder:text-muted-foreground"
          />
          <div className="flex gap-0.5 rounded-[10px] bg-accent p-[3px]">
            {SOURCES.map((choice) => (
              <button
                key={choice.label}
                type="button"
                onClick={() => setSource(choice.source)}
                className={cn(
                  "flex h-7 items-center gap-1.5 rounded-[7px] px-2.5 text-muted-foreground",
                  source === choice.source &&
                    "bg-background font-semibold text-foreground shadow-sm",
                )}
              >
                {choice.label}
                <span className="font-normal text-muted-foreground">
                  {
                    skills.filter(
                      (skill) =>
                        choice.source === null || skill.source === choice.source,
                    ).length
                  }
                </span>
              </button>
            ))}
          </div>
        </div>
        <div className="overflow-hidden rounded-xl border border-border">
          {shown.map((skill) => (
            <div
              key={skill.name}
              className="flex items-start gap-4 border-t border-border px-4 py-2.5 first:border-t-0"
            >
              <div className="min-w-0 flex-1">
                <Switch
                  data-skill={skill.name}
                  label={skill.name}
                  checked={skill.on}
                  onToggle={() => onToggle(skill)}
                />
                <p className="truncate pl-11 text-xs text-muted-foreground">
                  {skill.description}
                </p>
              </div>
              <span className="w-36 shrink-0 truncate pt-2 text-xs text-muted-foreground">
                {SOURCE_NAMES[skill.source]}
              </span>
            </div>
          ))}
        </div>
      </div>
    </main>
  );
}

// The agent asks to keep a skill it wrote. Nothing is saved until the user
// says so.
function SkillProposalCard({ proposal }: { proposal: SkillProposal }) {
  return (
    <div className="rounded-xl border border-border p-3.5">
      <div className="flex items-center gap-2 text-sm font-semibold">
        <Sparkles className="size-4 text-info" />
        Save a skill?
      </div>
      <div className="mt-2.5 rounded-lg bg-accent px-3 py-2.5 text-xs leading-relaxed">
        <div className="font-semibold">{proposal.name}</div>
        <div className="text-muted-foreground">{proposal.description}</div>
      </div>
      <div className="mt-3 flex gap-2">
        <Button
          id="save-skill"
          variant="primary"
          size="compact"
          onClick={() => chatHandler.answerSkill(true)}
        >
          Save skill
        </Button>
        <Button
          id="skip-skill"
          variant="secondary"
          size="compact"
          onClick={() => chatHandler.answerSkill(false)}
        >
          Not now
        </Button>
      </div>
    </div>
  );
}

// A step that waits for the user. Nothing on a page can answer it.
function ApprovalCard({ approval }: { approval: Approval }) {
  const answer = (given: AskUserAnswer | undefined) => {
    const choice = given?.selectedIds[0];
    chatHandler.answerApproval(
      choice === "approve"
        ? ApprovalChoice.kApprove
        : choice === "stop"
          ? ApprovalChoice.kStop
          : ApprovalChoice.kDecline,
      given?.otherText ?? "",
    );
  };
  return (
    <div id="approval" className="flex flex-col gap-1.5">
      <div className="px-1 text-xs text-muted-foreground">
        {approval.site} · Tab {approval.tab}
      </div>
      <AskUserQuestions
        questions={[
          {
            id: "approval",
            title: `${approval.action}?`,
            options: [
              { id: "approve", title: "Approve", description: approval.reason },
              { id: "decline", title: "Not this one" },
              { id: "stop", title: "Stop the chat" },
            ],
            allowOther: true,
            otherPlaceholder: "Tell the agent why, or what to do instead",
          },
        ]}
        onComplete={(answers) => answer(answers.approval)}
      />
    </div>
  );
}

// The skill the agent loaded since the user's last message, if any.
function usingSkill(lines: Step[]): string {
  let name = "";
  for (const line of lines) {
    if (line.kind === StepKind.kUser) {
      name = "";
    } else if (line.kind === StepKind.kTool && line.tool === "skill") {
      name = JSON.parse(line.args).name ?? "";
    }
  }
  return name;
}

// The agent's tab a step acted in: the one it named, or the one it opened.
function tabOf(step: Step): number {
  if (step.kind !== StepKind.kTool) {
    return 0;
  }
  const result = JSON.parse(step.result);
  return result.error ? 0 : (JSON.parse(step.args).tab ?? result.tab ?? 0);
}

// What the agent last did in each of its tabs, in the chat's words.
function lastSteps(lines: Step[]): Map<number, string> {
  const last = new Map<number, string>();
  for (const line of lines) {
    const number = tabOf(line);
    if (number) {
      last.set(number, describe(line));
    }
  }
  return last;
}

function TabCard({
  tab,
  current,
  working,
  waiting,
  doing,
}: {
  tab: AgentTab;
  current: boolean;
  working: boolean;
  waiting: boolean;
  doing: string;
}) {
  return (
    <button
      type="button"
      data-tab={tab.number}
      disabled={tab.closed}
      onClick={() => chatHandler.showTab(tab.number)}
      className={cn(
        "tab-card rounded-xl border border-border bg-background p-1 text-left",
        current && !tab.closed && "border-focus-ring/45 ring-3 ring-focus-ring/10",
        tab.closed && "opacity-60",
      )}
    >
      <div className="h-[136px] overflow-hidden rounded-lg bg-surface-1">
        {tab.picture && (
          <img
            src={tab.picture}
            alt=""
            className="h-full w-full object-cover object-top"
          />
        )}
      </div>
      <div className="px-1.5 pt-2 pb-1">
        <div className="flex items-center gap-1.5 font-medium">
          {tab.favicon && <img src={tab.favicon} alt="" className="size-3.5" />}
          <span className="tab-title truncate">{tab.title || tab.url}</span>
        </div>
        <div className="mt-1 flex items-center gap-1.5 text-xs text-muted-foreground">
          {current && working && !waiting && !tab.closed && (
            <Loader2 className="size-3 shrink-0 animate-spin" />
          )}
          {waiting && <span className="size-1.5 shrink-0 rounded-full bg-warning" />}
          <span className="tab-doing truncate">
            {tab.closed ? "Closed" : waiting ? "Waits for your approval" : doing}
          </span>
          <span className="ml-auto shrink-0">Tab {tab.number}</span>
        </div>
      </div>
    </button>
  );
}

function SideBySideButton({ on }: { on: boolean }) {
  return (
    <Button
      data-side-by-side=""
      variant="secondary"
      size="compact"
      leadingIcon={Columns2}
      active={on}
      aria-pressed={on}
      onClick={() => chatHandler.setSideBySide(!on, 0)}
    >
      Side by side
    </Button>
  );
}

// The agent's tabs: cards on the right of a wide page, and a strip of small
// tabs above the chat when the page is narrow or sits beside a tab.
function AgentTabs({
  tabs,
  current,
  working,
  waiting,
  sideBySide,
  lines,
}: {
  tabs: AgentTab[];
  current: number;
  working: boolean;
  // The tab whose step waits for approval, or 0.
  waiting: number;
  sideBySide: boolean;
  lines: Step[];
}) {
  const doing = lastSteps(lines);
  return (
    <aside
      className={cn(
        "hidden w-64 shrink-0 flex-col gap-3 overflow-y-auto border-l border-border px-3.5 py-4",
        !sideBySide && "lg:flex",
      )}
    >
      <div className="flex h-7 items-center gap-2">
        <h2 className="text-sm font-semibold">Tabs</h2>
        <span className="text-muted-foreground">{tabs.length}</span>
        <span className="ml-auto">
          <SideBySideButton on={sideBySide} />
        </span>
      </div>
      {tabs.map((tab) => (
        <TabCard
          key={tab.number}
          tab={tab}
          current={tab.number === current}
          working={working}
          waiting={tab.number === waiting}
          doing={doing.get(tab.number) ?? ""}
        />
      ))}
      <p className="mt-auto text-xs text-muted-foreground">
        Click a card to go to its tab. Each picture is the tab after the
        agent's last step there.
      </p>
    </aside>
  );
}

function TabStrip({
  tabs,
  current,
  sideBySide,
}: {
  tabs: AgentTab[];
  current: number;
  sideBySide: boolean;
}) {
  return (
    <div
      className={cn(
        "mx-auto flex w-full max-w-[720px] items-center gap-2 px-7 pt-3",
        !sideBySide && "lg:hidden",
      )}
    >
      <div className="flex min-w-0 flex-1 gap-0.5 rounded-[10px] bg-accent p-[3px]">
        {tabs
          .filter((tab) => !tab.closed)
          .map((tab) => (
            <button
              key={tab.number}
              type="button"
              data-strip-tab={tab.number}
              onClick={() =>
                sideBySide
                  ? chatHandler.setSideBySide(true, tab.number)
                  : chatHandler.showTab(tab.number)
              }
              className={cn(
                "flex h-7 min-w-0 flex-1 items-center gap-1.5 rounded-[7px] px-2.5 text-muted-foreground",
                tab.number === current &&
                  "bg-background font-semibold text-foreground shadow-sm",
              )}
            >
              {tab.favicon && (
                <img src={tab.favicon} alt="" className="size-3.5" />
              )}
              <span className="truncate">{tab.title || tab.url}</span>
            </button>
          ))}
      </div>
      <SideBySideButton on={sideBySide} />
    </div>
  );
}

function Chat() {
  const [opened, setOpened] = useState(false);
  const [chatId, setChatId] = useState("");
  const [chats, setChats] = useState<ChatSummary[]>([]);
  const [agentTabs, setAgentTabs] = useState<AgentTab[]>([]);
  const [currentTab, setCurrentTab] = useState(0);
  const [sideBySide, setSideBySide] = useState(false);
  const [view, setView] = useState<View>("chat");
  const [skills, setSkills] = useState<SkillInfo[]>([]);
  const [skillsFolder, setSkillsFolder] = useState("");
  const [proposal, setProposal] = useState<SkillProposal | null>(null);
  const [approval, setApproval] = useState<Approval | null>(null);
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
      chatPage.onTabs.addListener(
        (tabs: AgentTab[], current: number, split: boolean) => {
          setAgentTabs(tabs);
          setCurrentTab(current);
          setSideBySide(split);
        },
      ),
      chatPage.onSkillProposal.addListener((asked: SkillProposal) =>
        setProposal(asked),
      ),
      chatPage.onApproval.addListener((asked: Approval) => setApproval(asked)),
      chatPage.onStep.addListener((step: Step) => {
        setApproval(null);
        if (step.tool === "save_skill") {
          setProposal(null);
        }
        setLines((before) => [...before, step]);
        if (step.kind !== StepKind.kTool) {
          setWorking(false);
        }
      }),
    ];
    connect();
    loadSkills();
    return () => listeners.forEach((id) => chatPage.removeListener(id));
  }, []);

  const loadSkills = () => {
    chatHandler.getSkills().then(({ skills, folder }) => {
      setSkills(skills);
      setSkillsFolder(folder);
    });
  };

  const toggleSkill = (skill: SkillInfo) => {
    chatHandler.setSkillOn(skill.name, !skill.on);
    setSkills((before) =>
      before.map((kept) =>
        kept.name === skill.name ? { ...kept, on: !kept.on } : kept,
      ),
    );
  };

  const showView = (next: View) => {
    if (next === "skills") {
      loadSkills();
    }
    setView(next);
  };

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
      <ChatList
        chats={chats}
        current={chatId}
        view={view}
        skillsOn={skills.filter((skill) => skill.on).length}
        onView={showView}
      />
      <SidebarInset className="flex h-full flex-row">
      {view === "skills" && (
        <SkillsView skills={skills} folder={skillsFolder} onToggle={toggleSkill} />
      )}
      <div className={cn("flex min-w-0 flex-1 flex-col", view === "skills" && "hidden")}>
      {agentTabs.length > 0 && (
        <TabStrip tabs={agentTabs} current={currentTab} sideBySide={sideBySide} />
      )}
      <main className="flex-1 overflow-y-auto">
        <div className="mx-auto flex max-w-[720px] flex-col gap-4 px-7 py-5">
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
          {proposal && <SkillProposalCard proposal={proposal} />}
          {approval && <ApprovalCard approval={approval} />}
          {working && !proposal && !approval && (
            <ThinkingIndicator id="working" />
          )}
          <div ref={end} />
        </div>
      </main>
      <div className="mx-auto w-full max-w-[720px] px-7 pb-4">
        {usingSkill(lines) && (
          <div className="mb-2 inline-flex h-6 items-center gap-1.5 rounded-md bg-accent px-2 text-xs text-muted-foreground">
            <Sparkles className="size-3" />
            Using {usingSkill(lines)}
          </div>
        )}
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
      </div>
      {agentTabs.length > 0 && view === "chat" && (
        <AgentTabs
          tabs={agentTabs}
          current={currentTab}
          working={working}
          waiting={approval?.tab ?? 0}
          sideBySide={sideBySide}
          lines={lines}
        />
      )}
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
