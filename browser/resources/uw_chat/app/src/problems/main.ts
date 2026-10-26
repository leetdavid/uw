// Watches one of uw's own pages for rage clicks, dead clicks, and errors the
// page did not catch, and tells uw, which sends them to PostHog only after the
// user said Yes to "Help fix uw?". Every uw page loads it beside its own
// script. It reads no text: only a control's tag, role, id, and aria-label.
import { type PageProblem, ProblemKind, ProblemReporter, type StackFrame } from "uw-problems-mojom";

// PostHog's rules: three clicks within a second, close together, are a rage
// click, and a click on a control after which nothing changes for 2.5 seconds
// is a dead click.
const RAGE_CLICKS = 3;
const RAGE_MS = 1000;
const RAGE_DISTANCE = 30;
const DEAD_MS = 2500;
const CONTROLS =
  'a, button, summary, [role="button"], [role="link"], [role="menuitem"], ' +
  '[role="tab"], [role="checkbox"], [role="switch"], [role="option"]';
// Chromium reports this when a page's layout settles over two frames. It is
// harmless and common.
const BENIGN = /^ResizeObserver loop/;

const reporter = ProblemReporter.getRemote();

function report(problem: Partial<PageProblem> & { kind: ProblemKind }) {
  reporter.report({
    tag: "",
    role: "",
    elementId: "",
    label: "",
    errorType: "",
    message: "",
    stack: [],
    ...problem,
  });
}

function control(event: Event): Element | undefined {
  return event
    .composedPath()
    .find((node): node is Element => node instanceof Element && node.matches(CONTROLS));
}

function element(kind: ProblemKind, target: Element) {
  report({
    kind,
    tag: target.localName,
    role: target.getAttribute("role") ?? "",
    elementId: target.id,
    label: target.getAttribute("aria-label") ?? "",
  });
}

let clicks: { x: number; y: number; time: number }[] = [];

function watchRage(event: MouseEvent) {
  clicks = clicks.filter(
    (click) =>
      event.timeStamp - click.time <= RAGE_MS &&
      Math.hypot(event.clientX - click.x, event.clientY - click.y) <= RAGE_DISTANCE,
  );
  clicks.push({ x: event.clientX, y: event.clientY, time: event.timeStamp });
  const target = control(event) ?? event.composedPath()[0];
  if (clicks.length >= RAGE_CLICKS && target instanceof Element) {
    clicks = [];
    element(ProblemKind.kRageClick, target);
  }
}

// Anything that shows the click did something: a change to the page or to
// any shadow root the click passed through, a scroll, a new selection, a new
// address, or focus leaving the window for a menu or a file picker.
function watchDead(event: MouseEvent) {
  const target = control(event);
  if (!target) {
    return;
  }
  let changed = false;
  const mark = () => {
    changed = true;
  };
  const observer = new MutationObserver(mark);
  for (const node of event.composedPath()) {
    if (node instanceof Document || node instanceof ShadowRoot) {
      observer.observe(node, { subtree: true, childList: true, attributes: true, characterData: true });
    }
  }
  const signals: [EventTarget, string][] = [
    [window, "scroll"],
    [document, "selectionchange"],
    [window, "blur"],
    [document, "visibilitychange"],
  ];
  for (const [source, type] of signals) {
    source.addEventListener(type, mark, { capture: true });
  }
  const address = location.href;
  setTimeout(() => {
    observer.disconnect();
    for (const [source, type] of signals) {
      source.removeEventListener(type, mark, { capture: true });
    }
    if (!changed && location.href === address && document.visibilityState === "visible") {
      element(ProblemKind.kDeadClick, target);
    }
  }, DEAD_MS);
}

// V8's stack lines, such as "    at send (chrome://chat/chat.js:1:2345)".
function frames(stack: string): StackFrame[] {
  const parsed: StackFrame[] = [];
  for (const line of stack.split("\n")) {
    const match = /^\s*at (?:(.*?) \()?(.+?):(\d+):(\d+)\)?$/.exec(line);
    if (match?.[2] && match[3] && match[4]) {
      parsed.push({
        function: match[1] ?? "",
        file: match[2],
        line: Number(match[3]),
        column: Number(match[4]),
      });
    }
  }
  return parsed;
}

function error(thrown: unknown, fallback: string) {
  const message = thrown instanceof Error ? thrown.message : fallback || String(thrown);
  if (BENIGN.test(message)) {
    return;
  }
  report({
    kind: ProblemKind.kError,
    errorType: thrown instanceof Error ? thrown.name : "Error",
    message,
    stack: thrown instanceof Error && thrown.stack ? frames(thrown.stack) : [],
  });
}

document.addEventListener(
  "click",
  (event) => {
    watchRage(event);
    watchDead(event);
  },
  { capture: true },
);
window.addEventListener("error", (event) => error(event.error, event.message));
window.addEventListener("unhandledrejection", (event) => error(event.reason, ""));
