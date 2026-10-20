// The uw://chat page: one chat with uw's agent. Built from Fluid
// Functionalism's parts; see the "(Fluid)" frames on the canvas page
// "Agent chats".
import "@/trusted-types";
import {
  Fragment,
  StrictMode,
  useEffect,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  Columns2,
  Loader2,
  Pause,
  Blocks,
  ImageIcon,
  Info,
  Link2,
  Plus,
  Shield,
  Sparkles,
  Square,
  Terminal as TerminalIcon,
  TriangleAlert,
  Trash2,
} from "lucide-react";
import { code } from "@streamdown/code";
import { defaultRehypePlugins, Streamdown, type Components } from "streamdown";
import {
  Banner,
  BannerAction,
  BannerActions,
  BannerDescription,
  BannerTitle,
} from "@/components/ui/banner";
import {
  AskUserQuestions,
  type AskUserAnswer,
} from "@/components/ui/ask-user-questions";
import { Button } from "@/components/ui/button";
import { ChatMessage } from "@/components/ui/chat-message";
import {
  DropdownContent,
  DropdownEmpty,
  DropdownMenu,
  DropdownSeparator,
  DropdownSub,
  DropdownSubContent,
  DropdownSubTrigger,
  DropdownTrigger,
} from "@/components/ui/dropdown";
import { MenuItem } from "@/components/ui/menu-item";
import {
  InputMessage,
  type QueuedMessage,
} from "@/components/ui/input-message";
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
import {
  ThinkingStep,
  ThinkingSteps,
  ThinkingStepsContent,
} from "@/components/ui/thinking-steps";
import { fontWeights } from "@/lib/font-weight";
import type { IconComponent, IconName } from "@/lib/icon-context";
import { cn } from "@/lib/utils";
import { EffortChip, ModelChip } from "./providers/model-chip";
import {
  PromptFields,
  SlashCommandMark,
  SlashMenu,
  missingArgument,
  pickedPrompt,
  useSlashMenu,
  type SlashItem,
} from "./slash-menu";
import { ModeChip } from "./mode-chip";
import { IMAGE_TYPES, MAX_IMAGES, imageData } from "./images";
import { ProviderIcon } from "./providers/provider-parts";
import {
  providersHandler,
  type ModelList,
  type Provider,
} from "./providers/providers-api";
import {
  ApprovalChoice,
  ApprovalMode,
  ChatState,
  ChatConnectorState,
  chatHandler,
  chatPage,
  connect,
  StepKind,
  type AgentTab,
  type Approval,
  type ChatConnector,
  type ChatModel,
  type ChatPrompt,
  type ChatSummary,
  type SkillProposal,
  type Step,
} from "@/chat-api";

// Fluid's .typeset styles an answer by its plain elements, and Streamdown's
// own classes would win over it, so prose renders bare. Code blocks keep
// Streamdown's, for their colors and copy button.
const PROSE_TAGS = [
  "h1",
  "h2",
  "h3",
  "h4",
  "h5",
  "h6",
  "p",
  "ul",
  "ol",
  "li",
  "blockquote",
  "hr",
  "strong",
  "em",
  "del",
  "thead",
  "tbody",
  "tr",
  "th",
  "td",
] as const;

const ANSWER_PARTS: Components = {
  ...Object.fromEntries(PROSE_TAGS.map((tag) => [tag, tag])),
  inlineCode: ({ node: _node, ...props }) => <code {...props} />,
  table: ({ node: _node, ...props }) => (
    <div className="typeset-scroll">
      <table {...props} />
    </div>
  ),
  // An answer loads nothing from the web, so an image shows its description.
  img: ({ alt }) => alt ?? null,
  // A link opens a tab under the chat. Any address but a web page's stays
  // text.
  a: ({ node: _node, href, children }) =>
    href && /^https?:\/\//i.test(href) ? (
      <a href={href} target="_blank">
        {children}
      </a>
    ) : (
      <span>{children}</span>
    ),
};

// The product's examples, which a new chat suggests until the language model
// has written suggestions from the user's own chats and sites.
const EXAMPLES = [
  "Did my last Vercel deploy pass?",
  "Get the logs of the latest crash on Railway",
  "Find subscriptions I could cancel",
];

// What the note under the composer says while the agent waits, by mode.
const MODE_NOTES: Record<ApprovalMode, string> = {
  [ApprovalMode.kAskFirst]:
    "Every click, typing, and command waits for your approval",
  [ApprovalMode.kAuto]:
    "Changes on a site and every command wait for your approval",
  [ApprovalMode.kFullAuto]:
    "Full auto: the agent acts without asking, except to run a command",
};

// How long the agent has worked on the current request, as the chat's
// header says it.
function workingFor(since: number): string {
  const minutes = Math.floor((Date.now() - since) / 60_000);
  return minutes < 1 ? "Working" : `Working · ${minutes} min`;
}

// A page's address as a step names it: its site and path, without the
// scheme, the port, or a closing slash.
function shortAddress(url: string): string {
  try {
    const { hostname, pathname } = new URL(url);
    return (hostname.replace(/^www\./, "") + pathname).replace(/\/$/, "");
  } catch {
    return url;
  }
}

function Command({ children }: { children: ReactNode }) {
  return (
    <code className="command font-mono text-[0.92em] text-foreground">
      {children}
    </code>
  );
}

// What a keys step sent: its text in quotes, then each key.
function keysSent(args: { text?: string; keys?: string[] }): string {
  return [
    ...(args.text ? [`"${args.text}"`] : []),
    ...(args.keys ?? []),
  ].join(" ");
}

// A terminal step the user declined, which did not happen.
// A connector's tool, as the model calls it: "linear__save_issue".
const connectorTool = (tool: string) => {
  const at = tool.indexOf("__");
  return at > 0 ? { connector: tool.slice(0, at), tool: tool.slice(at + 2) } : null;
};

function declined(step: Step): boolean {
  return (
    (step.tool === "run" || step.tool === "keys" || !!connectorTool(step.tool)) &&
    String(JSON.parse(step.result).error ?? "").startsWith(
      "The user did not approve",
    )
  );
}

// The first line of a connector tool's answer, short enough for a step.
function firstLine(text: string): string {
  const line = text.trim().split("\n")[0] ?? "";
  return line.length > 80 ? `${line.slice(0, 79)}…` : line;
}

// One step in plain words, with what it acted on in bold, as the chat and
// its tests show it.
function describe(step: Step): ReactNode {
  const args = JSON.parse(step.args);
  const result = JSON.parse(step.result);
  const inTab = args.tab ? ` in tab ${args.tab}` : "";
  if (declined(step) && connectorTool(step.tool)) {
    return (
      <>
        Didn't run <Command>{connectorTool(step.tool)?.tool}</Command> in{" "}
        {connectorTool(step.tool)?.connector}
      </>
    );
  }
  if (declined(step)) {
    return step.tool === "run" ? (
      <>
        Didn't run <Command>{args.command}</Command>
        {inTab}
      </>
    ) : (
      <>
        Didn't send <b>{keysSent(args)}</b>
        {inTab}
      </>
    );
  }
  if (result.error) {
    return `${step.tool}: ${result.error}`;
  }
  const element = result.element ?? `element ${args.element}`;
  switch (step.tool) {
    case "open":
      return (
        <>
          Opened <b>{shortAddress(args.url)}</b> in tab {result.tab}
        </>
      );
    case "navigate":
      return (
        <>
          Went to <b>{shortAddress(args.url)}</b> in tab {args.tab}
        </>
      );
    case "read":
      return (
        <>
          Read <b>{result.title || shortAddress(result.url)}</b> in tab{" "}
          {args.tab}
        </>
      );
    case "click":
      return (
        <>
          Clicked <b>{element}</b> in tab {args.tab}
        </>
      );
    case "type":
      return (
        <>
          Typed into <b>{element}</b> in tab {args.tab}
        </>
      );
    case "run":
      return result.running ? (
        <>
          Running <Command>{args.command}</Command> in tab {result.tab} ·{" "}
          {result.waited}
        </>
      ) : (
        <>
          Ran <Command>{args.command}</Command> in tab {result.tab}
        </>
      );
    case "screen":
      return (
        <>
          Read <b>{result.title || result.folder}</b> in tab {args.tab}
        </>
      );
    case "keys":
      return (
        <>
          Sent <b>{keysSent(args)}</b> in tab {args.tab}
        </>
      );
    case "recall":
      return (
        <>
          Looked again at <b>{result.result?.title || `step ${args.step}`}</b>
        </>
      );
    case "skill":
      return args.file ? (
        <>
          Read <b>{args.file}</b> of the {args.name} skill
        </>
      ) : (
        <>
          Used the <b>{args.name}</b> skill
        </>
      );
    case "save_skill":
      return (
        <>
          {result.saved ? "Saved" : "Did not save"} the <b>{args.name}</b>{" "}
          skill
        </>
      );
    case "connector":
      return (
        <>
          Loaded <b>{result.connector}</b>
          <span className="text-muted-foreground">
            {" "}
            · {result.tools.length} tool{result.tools.length === 1 ? "" : "s"}
          </span>
        </>
      );
    case "prompt":
      return (
        <>
          Used the <b>{args.name}</b> prompt
        </>
      );
    default: {
      const called = connectorTool(step.tool);
      if (!called) {
        return step.tool;
      }
      const said = [
        firstLine(result.text ?? ""),
        typeof result.images === "number"
          ? `${result.images} image${result.images === 1 ? "" : "s"}`
          : result.images,
      ].filter(Boolean);
      return (
        <>
          Used <Command>{called.tool}</Command>
          {said.length > 0 && (
            <span className="text-muted-foreground"> · {said.join(" · ")}</span>
          )}
        </>
      );
    }
  }
}

const TOOL_ICONS: Record<string, IconName> = {
  open: "plus",
  navigate: "arrow-right",
  read: "square-library",
  click: "corner-down-right",
  type: "pencil",
  run: "terminal",
  screen: "terminal",
  keys: "keyboard",
  recall: "rotate-ccw",
  skill: "lightbulb",
  save_skill: "check",
  connector: "link",
  prompt: "message-circle",
};

// A message from the user, with its images as data: URLs.
function userLine(text: string, images: string[]): Step {
  return {
    kind: StepKind.kUser,
    tool: "",
    args: "",
    result: "",
    text,
    model: null,
    images,
  };
}

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

// Steps read as quiet lines under the conversation, so the answer stands out:
// muted, with only what a step acted on in the text color.
function StepText({ children }: { children: ReactNode }) {
  return (
    <span
      className="text-muted-foreground [&_b]:text-foreground [&_b]:[font-variation-settings:'wght'_550,'opsz'_18]"
      style={{ fontVariationSettings: fontWeights.normal }}
    >
      {children}
    </span>
  );
}

function StepLine({ step, last }: { step: Step; last: boolean }) {
  if (step.kind === StepKind.kResumed) {
    return (
      <ThinkingStep
        className="step"
        icon="play"
        isLast={last}
        label={<StepText>Continued</StepText>}
      />
    );
  }
  const result = JSON.parse(step.result);
  if (declined(step)) {
    return (
      <ThinkingStep
        className="step declined"
        icon="x"
        isLast={last}
        label={<StepText>{describe(step)}</StepText>}
      >
        {result.user_said && (
          <p className="step-note mt-1 text-xs text-muted-foreground">
            {result.user_said}
          </p>
        )}
      </ThinkingStep>
    );
  }
  const failed = Boolean(result.error);
  return (
    <ThinkingStep
      className={failed ? "step error text-destructive" : "step"}
      icon={
        failed
          ? "x"
          : (TOOL_ICONS[step.tool] ?? (connectorTool(step.tool) ? "link" : "dot"))
      }
      isLast={last}
      label={failed ? describe(step) : <StepText>{describe(step)}</StepText>}
    >
      {!failed && step.tool === "run" && result.output && (
        <pre className="step-output mt-1.5 max-w-full overflow-x-auto rounded-lg bg-surface-1 px-2.5 py-1.5 font-mono text-[11.5px] leading-[1.5] text-muted-foreground">
          {outputPreview(result.output)}
        </pre>
      )}
      {!failed && step.tool === "run" && result.running && (
        <p className="still-running mt-1 text-xs text-muted-foreground">
          Still running after {result.waited}, so the agent went on.
        </p>
      )}
    </ThinkingStep>
  );
}

// A command's output under its step: its first and last lines when long.
function outputPreview(output: string): string {
  const lines = output.split("\n");
  return lines.length <= 12
    ? output
    : [
        ...lines.slice(0, 4),
        `… ${lines.length - 10} more lines`,
        ...lines.slice(-6),
      ].join("\n");
}

// Where the chat's model or its effort changed. See the frame "AI
// providers: every state (Fluid)".
function ModelLine({
  model,
  providers,
}: {
  model: ChatModel | null;
  providers: Provider[];
}) {
  const provider = providers.find((each) => each.id === model?.providerId);
  return (
    model && (
      <div className="model-line flex items-center gap-2.5 text-xs text-muted-foreground">
        <span className="h-px flex-1 bg-border" />
        {provider && <ProviderIcon preset={provider.preset} className="size-3" />}
        Now {model.model}
        {model.effort && ` · ${model.effort}`}
        <span className="h-px flex-1 bg-border" />
      </div>
    )
  );
}

function Line({
  line,
  last,
  providers,
  onContinue,
  onTryAgain,
}: {
  line: Step;
  last: boolean;
  providers: Provider[];
  onContinue: () => void;
  onTryAgain: () => void;
}) {
  switch (line.kind) {
    case StepKind.kModel:
      return <ModelLine model={line.model} providers={providers} />;
    case StepKind.kUser:
      return (
        <ChatMessage from="user" images={line.images}>
          {line.text && <span className="mine">{line.text}</span>}
        </ChatMessage>
      );
    case StepKind.kAnswer:
      return (
        <ChatMessage from="assistant">
          <Streamdown
            className="answer typeset"
            components={ANSWER_PARTS}
            controls={{
              code: { copy: true, download: false },
              table: false,
              mermaid: false,
            }}
            lineNumbers={false}
            mode="static"
            plugins={{ code }}
            // Without Streamdown's raw HTML step, HTML in an answer shows
            // as text.
            rehypePlugins={[defaultRehypePlugins.sanitize]}
          >
            {line.text}
          </Streamdown>
        </ChatMessage>
      );
    case StepKind.kFailed:
      return (
        <Banner status="error">
          <BannerTitle className="failed">{line.text}</BannerTitle>
          {last && (
            <BannerActions>
              <BannerAction id="try-again" variant="primary" onClick={onTryAgain}>
                Try again
              </BannerAction>
              <BannerAction id="providers" onClick={() => open("chrome://ai/providers")}>
                AI providers
              </BannerAction>
            </BannerActions>
          )}
        </Banner>
      );
    case StepKind.kStoppedByUser:
      return (
        <p className="stopped-by-user flex items-center gap-1.5 text-sm text-muted-foreground">
          <Square aria-hidden className="size-3.5" />
          {line.text}
        </p>
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
  skillsOn,
}: {
  chats: ChatSummary[];
  current: string;
  skillsOn: number | null;
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
          className="bg-accent"
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
      <SidebarFooter>
        <SidebarMenu>
          <SidebarMenuItem>
            <SidebarMenuButton
              id="skills"
              icon={Sparkles}
              onClick={() => providersHandler.showAiPage("skills")}
            >
              <span className="flex-1">Skills</span>
              <span className="text-muted-foreground tabular-nums">
                {skillsOn === null ? "" : `${skillsOn} on`}
              </span>
            </SidebarMenuButton>
          </SidebarMenuItem>
        </SidebarMenu>
      </SidebarFooter>
    </Sidebar>
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
// The card goes once answered: an approved command may run for minutes.
function ApprovalCard({
  approval,
  onAnswered,
}: {
  approval: Approval;
  onAnswered: () => void;
}) {
  const answer = (given: AskUserAnswer | undefined) => {
    const choice = given?.selectedIds[0];
    onAnswered();
    chatHandler.answerApproval(
      choice === "approve"
        ? ApprovalChoice.kApprove
        : choice === "chat"
          ? ApprovalChoice.kAllowForChat
          : choice === "always"
            ? ApprovalChoice.kAlwaysAllow
            : choice === "stop"
              ? ApprovalChoice.kStop
              : ApprovalChoice.kDecline,
      given?.otherText ?? "",
    );
  };
  const terminal = Boolean(approval.folder);
  const connector = Boolean(approval.connector);
  const options = [
    {
      id: "approve",
      title: connector ? "Allow once" : "Approve",
      description: terminal || connector ? undefined : approval.reason,
      menu: connector
        ? [
            { id: "chat", title: "Allow for this chat", description: "Until this chat ends" },
            ...(approval.canAlwaysAllow
              ? [
                  {
                    id: "always",
                    title: "Always allow",
                    description: "In every chat, until you take it back",
                  },
                ]
              : []),
          ]
        : undefined,
    },
    { id: "decline", title: "Not this one" },
    { id: "stop", title: "Stop the chat" },
  ];
  return (
    <div id="approval">
      <AskUserQuestions
        label={
          connector ? (
            <span className="flex items-center gap-1.5">
              <Link2 aria-hidden className="size-3.5" />
              {approval.connector} · <span className="font-mono text-xs">{approval.tool}</span>
            </span>
          ) : terminal ? (
            <span className="flex items-center gap-1.5">
              <TerminalIcon aria-hidden className="size-3.5" />
              Terminal · Tab {approval.tab} · {approval.folder}
            </span>
          ) : (
            `${approval.site} · Tab ${approval.tab}`
          )
        }
        questions={[
          {
            id: "approval",
            title: connector
              ? `Run ${approval.tool} in ${approval.connector}?`
              : terminal
                ? approval.command
                  ? "Run this command?"
                  : "Send keys to the running program?"
                : `${approval.action}?`,
            detail: connector ? (
              <ConnectorApproval approval={approval} />
            ) : (
              terminal && <TerminalApproval approval={approval} />
            ),
            options,
            allowOther: true,
            otherPlaceholder: "Tell the agent why, or what to do instead",
          },
        ]}
        onComplete={(answers) => answer(answers.approval)}
      />
    </div>
  );
}

// The composer's "+": images, and the connectors this chat uses.
function PlusMenu({
  connectors,
  canAttach,
  onAttach,
}: {
  connectors: ChatConnector[];
  canAttach: boolean;
  onAttach: () => void;
}) {
  const NOTES: Partial<Record<ChatConnectorState, string>> = {
    [ChatConnectorState.kNeedsSignIn]: "Sign in",
    [ChatConnectorState.kFailed]: "Failed",
    [ChatConnectorState.kStarting]: "Starting",
  };
  return (
    <DropdownMenu>
      <DropdownTrigger
        render={
          <button
            type="button"
            id="plus"
            aria-label="Add images or connectors"
            title="Add images or connectors"
            className="grid size-7 shrink-0 place-items-center rounded-lg bg-accent text-muted-foreground hover:text-foreground"
          >
            <Plus aria-hidden className="size-4" />
          </button>
        }
      />
      <DropdownContent side="top" align="start" className="w-[200px]">
        <MenuItem
          index={0}
          icon={ImageIcon}
          label="Add images"
          disabled={!canAttach}
          onSelect={onAttach}
        />
        <DropdownSub>
          <DropdownSubTrigger index={1} icon={Blocks} label="Connectors" />
          <DropdownSubContent className="w-[240px]">
            {connectors.length ? (
              connectors.map((connector, at) => (
                <MenuItem
                  key={connector.name}
                  index={at}
                  label={connector.name}
                  checked={connector.on}
                  closeOnClick={false}
                  data-chat-connector={connector.name}
                  data-note={NOTES[connector.state]}
                  className="after:ml-auto after:pl-2 after:text-[11.5px] after:text-muted-foreground after:content-[attr(data-note)]"
                  onSelect={() => chatHandler.setConnectorOn(connector.name, !connector.on)}
                />
              ))
            ) : (
              <DropdownEmpty>No connectors yet</DropdownEmpty>
            )}
            <DropdownSeparator />
            <MenuItem
              index={connectors.length}
              label="Manage connectors"
              onSelect={() => providersHandler.showAiPage("connectors")}
            />
          </DropdownSubContent>
        </DropdownSub>
      </DropdownContent>
    </DropdownMenu>
  );
}

// What a connector tool's approval shows: the arguments the model sent, then
// why it asks.
function ConnectorApproval({ approval }: { approval: Approval }) {
  let entries: [string, unknown][] = [];
  try {
    entries = Object.entries(JSON.parse(approval.arguments));
  } catch {}
  return (
    <div className="flex flex-col gap-2">
      {entries.length > 0 && (
        <dl className="approval-arguments grid grid-cols-[minmax(0,auto)_minmax(0,1fr)] gap-x-2.5 gap-y-0.5 rounded-lg bg-surface-1 px-3 py-2 text-[12.5px] leading-[1.45]">
          {entries.map(([key, value]) => (
            <Fragment key={key}>
              <dt className="font-mono text-[11.5px] text-muted-foreground">{key}</dt>
              <dd className="break-words text-foreground">
                {typeof value === "string" ? value : JSON.stringify(value)}
              </dd>
            </Fragment>
          ))}
        </dl>
      )}
      <p className="approval-reason flex gap-1.5 text-[12.5px] text-muted-foreground">
        <Info aria-hidden className="mt-0.5 size-3.5 shrink-0" />
        {approval.reason}
      </p>
    </div>
  );
}

// What a command's or keys' approval shows: the command after a prompt, or
// what the program last printed and the keys, then why it asks.
function TerminalApproval({ approval }: { approval: Approval }) {
  const [first, ...rest] = approval.reason.split(/(?<=\.)\s+/);
  return (
    <div className="flex flex-col gap-2">
      {approval.command ? (
        <pre className="approval-command whitespace-pre-wrap break-all rounded-lg bg-[#1e1b2e] px-3 py-2 font-mono text-[12.5px] leading-[1.5] text-[#edebf7]">
          <span className="text-[#8e8aa6]">% </span>
          {approval.command}
        </pre>
      ) : (
        <>
          {approval.screen && (
            <pre className="approval-screen whitespace-pre-wrap break-all rounded-lg bg-surface-1 px-3 py-2 font-mono text-[11.5px] leading-[1.5] text-muted-foreground">
              {approval.screen}
            </pre>
          )}
          <div className="approval-keys flex flex-wrap items-center gap-1 text-xs text-muted-foreground">
            {approval.text && (
              <kbd className="rounded-md border border-border px-1.5 py-0.5 font-mono text-foreground">
                {approval.text}
              </kbd>
            )}
            {approval.keys.map((key, at) => (
              <kbd
                key={at}
                className="rounded-md border border-border px-1.5 py-0.5 font-sans text-foreground"
              >
                {key}
              </kbd>
            ))}
          </div>
        </>
      )}
      <p className="approval-reason flex gap-1.5 text-[13px] leading-[1.45] text-muted-foreground">
        <Shield aria-hidden className="mt-0.5 size-3.5 shrink-0 text-warning" />
        <span>
          <b className="font-semibold text-foreground">{first}</b>
          {rest.length > 0 && ` ${rest.join(" ")}`}
        </span>
      </p>
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
function lastSteps(lines: Step[]): Map<number, ReactNode> {
  const last = new Map<number, ReactNode>();
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
  // What the tab's step waits for, if anything.
  waiting: "approval" | "password" | null;
  doing: ReactNode;
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
        waiting && "border-warning/45 ring-3 ring-warning/10",
        tab.closed && "opacity-60",
      )}
    >
      {tab.terminal ? (
        <pre className="tab-screen flex h-[136px] flex-col justify-end overflow-hidden whitespace-pre-wrap break-all rounded-lg bg-surface-1 px-2 py-1.5 font-mono text-[8.5px] leading-[1.5] text-muted-foreground">
          {tab.screen}
        </pre>
      ) : (
        <div className="h-[136px] overflow-hidden rounded-lg bg-surface-1">
          {tab.picture && (
            <img
              src={tab.picture}
              alt=""
              className="h-full w-full object-cover object-top"
            />
          )}
        </div>
      )}
      <div className="px-1.5 pt-2 pb-1">
        <div className="flex items-center gap-1.5 font-medium">
          {tab.terminal ? (
            <TerminalIcon aria-hidden className="size-3.5 shrink-0" />
          ) : (
            tab.favicon && <img src={tab.favicon} alt="" className="size-3.5" />
          )}
          <span className="tab-title truncate">{tab.title || tab.url}</span>
        </div>
        <div className="mt-1 flex items-center gap-1.5 text-xs text-muted-foreground">
          {current && working && !waiting && !tab.closed && (
            <Loader2 className="size-3 shrink-0 animate-spin" />
          )}
          {waiting && <span className="size-1.5 shrink-0 rounded-full bg-warning" />}
          <span className="tab-doing truncate [&_b]:font-normal [&_code]:text-muted-foreground">
            {tab.closed
              ? "Closed"
              : waiting
                ? `Waits for your ${waiting}`
                : doing}
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
  waitingFor,
  sideBySide,
  lines,
}: {
  tabs: AgentTab[];
  current: number;
  working: boolean;
  // The tab whose step waits for approval, or for a password, or 0.
  waiting: number;
  waitingFor: "approval" | "password";
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
          waiting={tab.number === waiting ? waitingFor : null}
          doing={doing.get(tab.number) ?? ""}
        />
      ))}
      <p className="mt-auto text-xs text-muted-foreground">
        {tabs.some((tab) => tab.terminal)
          ? "The agent's commands run in its own terminal, under this chat, " +
            "in your login shell. You can type there, or press Ctrl-C. It " +
            "never uses a terminal you opened."
          : "Click a card to go to its tab. Each picture is the tab after " +
            "the agent's last step there."}
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
  // Text the command bar opened this chat to ask, until it is sent.
  const [asked, setAsked] = useState<string | null>(null);
  const [chatId, setChatId] = useState("");
  const [chats, setChats] = useState<ChatSummary[]>([]);
  const [agentTabs, setAgentTabs] = useState<AgentTab[]>([]);
  const [currentTab, setCurrentTab] = useState(0);
  const [sideBySide, setSideBySide] = useState(false);
  const [skillsOn, setSkillsOn] = useState<number | null>(null);
  // What the / menu offers: the skills the user may start.
  const [slashItems, setSlashItems] = useState<SlashItem[]>([]);
  // The connectors that are on, and whether this chat uses each.
  const [connectors, setConnectors] = useState<ChatConnector[]>([]);
  const [proposal, setProposal] = useState<SkillProposal | null>(null);
  const [approval, setApproval] = useState<Approval | null>(null);
  // A program in the agent's terminal tab that waits for the user's password.
  const [password, setPassword] = useState<{
    tab: number;
    program: string;
  } | null>(null);
  const [lines, setLines] = useState<Step[]>([]);
  // The model the chat runs on, the providers it can pick from, and each
  // provider's models once listed.
  const [model, setModel] = useState<ChatModel | null>(null);
  const [mode, setMode] = useState(ApprovalMode.kAuto);
  const [queue, setQueue] = useState<QueuedMessage[]>([]);
  const [files, setFiles] = useState<File[]>([]);
  const [providers, setProviders] = useState<Provider[] | null>(null);
  const [lists, setLists] = useState<Record<string, ModelList | undefined>>(
    {},
  );
  const listed = useRef(new Set<string>());
  const [working, setWorking] = useState(false);
  const [since, setSince] = useState(0);
  const [, setNow] = useState(0);
  const [draft, setDraft] = useState("");
  // A prompt's argument values, while one is picked.
  const [promptValues, setPromptValues] = useState<Record<string, string>>({});
  const menuItems: SlashItem[] = [
    ...slashItems,
    ...connectors
      .filter((connector) => connector.on)
      .flatMap((connector) =>
        connector.prompts.map((prompt) => ({
          command: `${connector.name}:${prompt.name}`,
          description: prompt.description,
          prompt: { connector: connector.name, prompt },
        })),
      ),
  ];
  const slash = useSlashMenu(draft, setDraft, menuItems);
  const promptItem = pickedPrompt(draft, menuItems);
  const composerBox = useRef<HTMLDivElement>(null);
  const [suggestions, setSuggestions] = useState(EXAMPLES);
  const end = useRef<HTMLDivElement>(null);

  // Keeps the header's minutes current while the agent works.
  useEffect(() => {
    if (!working) {
      return;
    }
    const timer = setInterval(() => setNow(Date.now()), 15_000);
    return () => clearInterval(timer);
  }, [working]);

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
      chatPage.onModel.addListener((picked: ChatModel | null) =>
        setModel(picked),
      ),
      chatPage.onMode.addListener((picked: ApprovalMode) => setMode(picked)),
      chatPage.onChats.addListener((saved: ChatSummary[]) => setChats(saved)),
      chatPage.onAsk.addListener((text: string) => setAsked(text)),
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
      chatPage.onConnectorsChanged.addListener(() => loadConnectors()),
      chatPage.onPassword.addListener((tab: number, program: string) =>
        setPassword(tab ? { tab, program } : null),
      ),
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
    loadConnectors();
    loadProviders();
    chatHandler.getSuggestions().then(({ suggestions }) => {
      if (suggestions.length) {
        setSuggestions(suggestions);
      }
    });
    return () => listeners.forEach((id) => chatPage.removeListener(id));
  }, []);

  const loadProviders = async () => {
    const { state } = await providersHandler.getState();
    const usable = state.providers.filter((provider) => provider.servesLanguage);
    setProviders(usable);
    return usable;
  };

  // A provider is listed once; one whose list failed is asked again the next
  // time the menu opens.
  const listModels = (providerId: string) => {
    if (listed.current.has(providerId)) {
      return;
    }
    listed.current.add(providerId);
    providersHandler.listModels(providerId).then(({ list }) => {
      if (!list.ok) {
        listed.current.delete(providerId);
      }
      setLists((before) => ({ ...before, [providerId]: list }));
    });
  };

  // The chat's own provider is listed at once, for its model's levels; the
  // others when the model menu opens, which also picks up new providers.
  useEffect(() => {
    if (model) {
      listModels(model.providerId);
    }
  }, [model?.providerId]);

  // Marks the switch in the chat once it has begun, as the browser does:
  // switches in a row make one line.
  const switchTo = (picked: ChatModel) => {
    setModel(picked);
    if (!lines.some((line) => line.kind === StepKind.kUser)) {
      return;
    }
    const line: Step = {
      kind: StepKind.kModel,
      tool: "",
      args: "",
      result: "",
      text: "",
      model: picked,
      images: [],
    };
    setLines((before) =>
      before.at(-1)?.kind === StepKind.kModel
        ? [...before.slice(0, -1), line]
        : [...before, line],
    );
  };

  const pickModel = (provider: Provider, name: string) => {
    switchTo({ providerId: provider.id, model: name, effort: "" });
    chatHandler.setModel(provider.id, name);
  };

  const pickEffort = (effort: string) => {
    if (model) {
      switchTo({ ...model, effort });
      chatHandler.setEffort(effort);
    }
  };

  const loadConnectors = () => {
    chatHandler.getConnectors().then(({ connectors }) => setConnectors(connectors));
  };

  const loadSkills = () => {
    providersHandler.getSkills().then(({ skills }) => {
      setSkillsOn(skills.filter((skill) => skill.on).length);
      setSlashItems(
        skills
          .filter((skill) => skill.on && skill.forUser)
          .map((skill) => ({
            command: skill.name,
            description: skill.description,
            tag: skill.forAgent ? undefined : "Only you",
            hint: skill.argumentHint || undefined,
          })),
      );
    });
  };

  useEffect(() => {
    end.current?.scrollIntoView({ block: "end" });
  }, [lines, working]);

  const chosen = providers?.find(
    (provider) => provider.id === model?.providerId,
  );
  const chosenList = model ? lists[model.providerId] : undefined;
  const chosenModel = chosenList?.models.find(
    (each) => each.name === model?.model,
  );
  // The chat waits for another model once its own is gone.
  const gone =
    !model || !providers
      ? ""
      : !chosen
        ? `${model.model}'s provider was removed.`
        : chosenList?.ok && !chosenModel
          ? `${model.model} is no longer offered by ${chosen.name}.`
          : "";

  // The chat's model takes no images, as the model catalog says.
  // The browser leaves a connector's images out for a model that can't see
  // them.
  useEffect(() => {
    chatHandler.setSeesImages(chosenModel?.seesImages !== false);
  }, [chosenModel?.seesImages]);

  const cannotSee = (attached: File[]) =>
    attached.length > 0 && chosenModel?.seesImages === false;

  const send = async (text: string, attached: File[] = []) => {
    const message = text.trim();
    if (
      (!message && !attached.length) ||
      working ||
      !opened ||
      gone ||
      cannotSee(attached) ||
      missingArgument(promptItem, promptValues)
    ) {
      return;
    }
    setWorking(true);
    setSince(Date.now());
    const images = await Promise.all(attached.map(imageData));
    // The first message names the tab.
    if (message && !lines.some((line) => line.kind === StepKind.kUser)) {
      document.title = message;
    }
    if (promptItem?.prompt) {
      const values = Object.fromEntries(
        promptItem.prompt.prompt.arguments
          .filter((argument) => promptValues[argument.name]?.trim())
          .map((argument) => [argument.name, promptValues[argument.name].trim()]),
      );
      const shown = [
        `/${promptItem.command}`,
        ...Object.entries(values).map(([name, value]) => `${name}: ${value}`),
      ].join("\n");
      setLines((before) => [...before, userLine(shown, [])]);
      setDraft("");
      setPromptValues({});
      chatHandler.sendPrompt(
        promptItem.prompt.connector,
        promptItem.prompt.prompt.name,
        values,
        shown,
      );
      return;
    }
    setLines((before) => [...before, userLine(message, images)]);
    setDraft("");
    setFiles([]);
    chatHandler.send(message, images);
  };

  // A message while the agent works: it goes in after the step under way, or
  // declines a step that waits for approval.
  const steer = async (text: string, attached: File[]) => {
    const message = text.trim();
    if ((!message && !attached.length) || cannotSee(attached)) {
      return;
    }
    const images = await Promise.all(attached.map(imageData));
    setLines((before) => [...before, userLine(message, images)]);
    chatHandler.steer(message, images);
  };

  // Sends what the command bar asked, as if typed, once the chat is open.
  useEffect(() => {
    if (asked && opened) {
      setAsked(null);
      send(asked);
    }
  }, [asked, opened]);

  const resume = () => {
    setLines((before) => [
      ...before,
      {
        kind: StepKind.kResumed,
        tool: "",
        args: "",
        result: "",
        text: "",
        model: null,
        images: [],
      },
    ]);
    setWorking(true);
    setSince(Date.now());
    chatHandler.resume();
  };

  const tryAgain = () => {
    setWorking(true);
    setSince(Date.now());
    chatHandler.tryAgain();
  };

  const groups = group(lines);
  // A model switch is never what the chat ends on.
  const lastGroup = groups.findLastIndex(
    (run) => run[0]!.kind !== StepKind.kModel,
  );
  const title = lines.find((line) => line.kind === StepKind.kUser)?.text;
  const stopped =
    !working &&
    lines.findLast((line) => line.kind !== StepKind.kModel)?.kind ===
      StepKind.kStopped;
  // Queued messages wait after a run that ended in a failure or a stop.
  const ended = lines.findLast((line) => line.kind !== StepKind.kModel)?.kind;
  const holdQueue =
    ended === StepKind.kFailed || ended === StepKind.kStoppedByUser;
  const state = approval
    ? "Waits for your approval"
    : password
      ? "Waits for your password"
      : working
        ? workingFor(since)
        : stopped
          ? "Stopped"
          : "";
  const chips = providers && (
    <div id="model-chips" className="flex min-w-0 items-center gap-1.5">
      <ModelChip
        choice={model}
        providers={providers}
        lists={lists}
        needsTools
        gone={Boolean(gone)}
        manage
        className="h-7 max-w-[240px]"
        onOpen={async () => {
          for (const provider of await loadProviders()) {
            listModels(provider.id);
          }
        }}
        onPick={pickModel}
      />
      <EffortChip
        efforts={chosenModel?.efforts ?? []}
        effort={model?.effort ?? ""}
        model={model?.model}
        preset={chosen?.preset}
        className="h-7"
        onPick={pickEffort}
      />
      <ModeChip
        mode={mode}
        className="h-7"
        onPick={(picked) => {
          setMode(picked);
          chatHandler.setMode(picked);
        }}
      />
    </div>
  );
  // Why the images cannot go yet, or that the limit is reached.
  const notice = cannotSee(files) ? (
    <p
      id="cannot-see-images"
      className="flex items-center gap-1.5 px-1 text-[12.5px] text-warning"
    >
      <TriangleAlert aria-hidden className="size-3.5 shrink-0" />
      {model?.model} can't see images. Pick a model that can.
    </p>
  ) : files.length >= MAX_IMAGES ? (
    <p className="px-1 text-xs text-muted-foreground">
      A message holds up to {MAX_IMAGES} images.
    </p>
  ) : null;
  const composer = (
    <>
      {gone && (
        <Banner id="model-gone" status="warning" className="mb-2">
          <BannerTitle>{gone}</BannerTitle>
          <BannerDescription>Pick a model to go on with this chat.</BannerDescription>
        </Banner>
      )}
      <div
        ref={composerBox}
        className="relative"
        onKeyDownCapture={slash.onKeyDownCapture}
      >
      <SlashMenu shown={slash.shown} highlight={slash.highlight} onPick={slash.pick} />
      {promptItem?.prompt && (
        <PromptFields
          prompt={promptItem.prompt.prompt}
          values={promptValues}
          onChange={setPromptValues}
          onSubmit={() => send(draft)}
        />
      )}
      <InputMessage
        value={draft}
        onValueChange={setDraft}
        onSend={send}
        onSteer={steer}
        onStop={() => chatHandler.stop()}
        queue={queue}
        onQueueChange={setQueue}
        holdQueue={holdQueue}
        disabled={!opened}
        status={working ? "streaming" : "idle"}
        placeholder="Reply to the agent"
        placeholderSuggestion={title ? undefined : suggestions[0]}
        suggestions={title ? undefined : suggestions.slice(1)}
        textareaProps={{ id: "message", "aria-label": "Message" }}
        files={files}
        onFilesChange={setFiles}
        accept={IMAGE_TYPES}
        maxFiles={MAX_IMAGES}
        filePreviewSize={64}
        notice={notice}
        sendBlocked={cannotSee(files) || missingArgument(promptItem, promptValues)}
        leftSlot={({ openFilePicker }) => (
          <div className="flex min-w-0 items-center gap-1.5">
            <PlusMenu
              connectors={connectors}
              canAttach={files.length < MAX_IMAGES}
              onAttach={() => openFilePicker(IMAGE_TYPES)}
            />
            {chips}
          </div>
        )}
      />
      <SlashCommandMark draft={draft} items={menuItems} field={composerBox} />
      </div>
      <p id="approval-note" className="px-1 pt-2 text-right text-xs text-muted-foreground">
        {approval
          ? "Enter declines the step with this note · Cmd+Enter queues"
          : working
            ? "Enter steers · Cmd+Enter queues"
            : MODE_NOTES[mode]}
      </p>
    </>
  );
  return (
    <SidebarProvider persist={false} width="15rem" className="h-full">
      <ChatList
        chats={chats}
        current={chatId}
        skillsOn={skillsOn}
      />
      <SidebarInset className="flex h-full flex-row">
      <div className={cn("flex min-w-0 flex-1 flex-col", !title && "justify-center")}>
      {agentTabs.length > 0 && (
        <TabStrip tabs={agentTabs} current={currentTab} sideBySide={sideBySide} />
      )}
      {title && (
        <header className="mx-auto flex w-full max-w-[720px] items-center gap-2.5 px-7 pt-4">
          <h1 id="chat-title" className="truncate text-base font-semibold">
            {title}
          </h1>
          {state && (
            <span
              id="chat-state"
              className="inline-flex h-6 shrink-0 items-center gap-1.5 rounded-full bg-accent px-2.5 text-xs text-muted-foreground"
            >
              {approval || password ? (
                <span className="size-1.5 rounded-full bg-warning" />
              ) : working ? (
                <Loader2 className="size-3 animate-spin" />
              ) : (
                <Pause className="size-3" />
              )}
              {state}
            </span>
          )}
        </header>
      )}
      {!title && (
        <div id="start" className="px-7 pb-5 text-center">
          <h1 className="text-xl font-semibold">What should the agent do?</h1>
          <p className="mt-1 text-muted-foreground">
            It works in tabs under this chat and never takes your selection.
          </p>
        </div>
      )}
      <main className={cn("flex-1 overflow-y-auto", !title && "hidden")}>
        <div className="mx-auto flex max-w-[720px] flex-col gap-4 px-7 py-5">
          {groups.map((run, index) =>
            isStep(run[0]!) ? (
              <ThinkingSteps key={index} className="w-full">
                <ThinkingStepsContent>
                  {run.map((step, at) => (
                    <StepLine key={at} step={step} last={at === run.length - 1} />
                  ))}
                </ThinkingStepsContent>
              </ThinkingSteps>
            ) : (
              <Fragment key={index}>
                <Line
                  line={run[0]!}
                  last={index === lastGroup && !working}
                  providers={providers ?? []}
                  onContinue={resume}
                  onTryAgain={tryAgain}
                />
              </Fragment>
            ),
          )}
          {proposal && <SkillProposalCard proposal={proposal} />}
          {password && (
            <Banner id="password" status="warning">
              <BannerTitle>
                {password.program} wants your password in tab {password.tab}.
              </BannerTitle>
              <BannerDescription>
                Type it in the tab. The agent never types passwords.
              </BannerDescription>
              <BannerActions>
                <BannerAction
                  id="go-to-password"
                  variant="primary"
                  onClick={() => chatHandler.showTab(password.tab)}
                >
                  Go to tab {password.tab}
                </BannerAction>
                <BannerAction id="stop-for-password" onClick={() => chatHandler.stop()}>
                  Stop the chat
                </BannerAction>
              </BannerActions>
            </Banner>
          )}
          {approval && (
            <ApprovalCard
              approval={approval}
              onAnswered={() => setApproval(null)}
            />
          )}
          {working && !proposal && !approval && !password && (
            <ThinkingIndicator id="working" />
          )}
          <div ref={end} />
        </div>
      </main>
      <div
        className={cn(
          "mx-auto w-full px-7",
          title ? "max-w-[720px] pb-3" : "max-w-[600px] pb-24",
        )}
      >
        {usingSkill(lines) && (
          <div className="mb-2 inline-flex h-6 items-center gap-1.5 rounded-md bg-accent px-2 text-xs text-muted-foreground">
            <Sparkles className="size-3" />
            Using {usingSkill(lines)}
          </div>
        )}
        {composer}
      </div>
      </div>
      {agentTabs.length > 0 && (
        <AgentTabs
          tabs={agentTabs}
          current={currentTab}
          working={working}
          waiting={approval?.tab ?? password?.tab ?? 0}
          waitingFor={approval ? "approval" : "password"}
          sideBySide={sideBySide}
          lines={lines}
        />
      )}
      </SidebarInset>
    </SidebarProvider>
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
      <Chat />
    </MotionConfig>
  </StrictMode>,
);
