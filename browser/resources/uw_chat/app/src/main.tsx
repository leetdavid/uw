// The uw://chat page: one chat with uw's agent. Built from Fluid
// Functionalism's parts; see the "(Fluid)" frames on the canvas page
// "Agent chats".
import { Fragment, StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";
import {
  Banner,
  BannerAction,
  BannerActions,
  BannerTitle,
} from "@/components/ui/banner";
import { ChatMessage } from "@/components/ui/chat-message";
import { InputMessage } from "@/components/ui/input-message";
import { ThinkingIndicator } from "@/components/ui/thinking-indicator";
import {
  ThinkingStep,
  ThinkingSteps,
  ThinkingStepsContent,
} from "@/components/ui/thinking-steps";
import type { IconName } from "@/lib/icon-context";
import {
  chatHandler,
  chatPage,
  connect,
  StepKind,
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

function Chat() {
  const [opened, setOpened] = useState(false);
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
          setOpened(true);
          // Tells tests, and anyone looking, which chat the page holds.
          document.body.dataset.chat = id;
        },
      ),
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
    <div className="flex h-full flex-col">
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
      <Chat />
    </MotionConfig>
  </StrictMode>,
);
