// Key hints on the chat page: while Cmd is held with the pointer on a link in
// an answer, a label by the pointer says a click opens it in the background.
// See docs/plans/keyboard-and-mouse.md#key-hints and the frame "Key hints:
// every state".
import {
  useEffect,
  useState,
  useSyncExternalStore,
  type ReactNode,
} from "react";

let cmdHeld = false;
const listeners = new Set<() => void>();

function setCmdHeld(held: boolean) {
  if (held !== cmdHeld) {
    cmdHeld = held;
    listeners.forEach((listener) => listener());
  }
}

window.addEventListener("keydown", (event) => setCmdHeld(event.metaKey));
window.addEventListener("keyup", (event) => setCmdHeld(event.metaKey));
window.addEventListener("pointermove", (event) => setCmdHeld(event.metaKey));
// Switching to another app or window lets go of the key.
window.addEventListener("blur", () => setCmdHeld(false));

function useCmdHeld(): boolean {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    () => cmdHeld,
  );
}

// A link in an answer. Its tooltip names Cmd until Cmd is held, when the
// label takes its place.
export function AnswerLink({
  href,
  children,
}: {
  href: string;
  children: ReactNode;
}) {
  const held = useCmdHeld();
  return (
    <a
      href={href}
      target="_blank"
      data-answer-link=""
      title={held ? undefined : "Cmd-click to open in the background"}
    >
      {children}
    </a>
  );
}

// The label by the pointer, while Cmd is held on an answer's link.
export function LinkHint() {
  const held = useCmdHeld();
  const [at, setAt] = useState<{ x: number; y: number } | null>(null);
  useEffect(() => {
    const move = (event: PointerEvent) => {
      const onLink =
        event.target instanceof Element &&
        event.target.closest("a[data-answer-link]");
      setAt(onLink ? { x: event.clientX, y: event.clientY } : null);
    };
    const leave = () => setAt(null);
    window.addEventListener("pointermove", move);
    document.addEventListener("pointerleave", leave);
    return () => {
      window.removeEventListener("pointermove", move);
      document.removeEventListener("pointerleave", leave);
    };
  }, []);
  if (!held || !at) {
    return null;
  }
  return (
    <div
      id="link-hint"
      role="status"
      className="pointer-events-none fixed z-50 rounded-[7px] bg-[#1e1b2e]/92 px-2.5 py-1 text-xs whitespace-nowrap text-white shadow-[0_4px_12px_rgba(30,27,46,0.2)]"
      style={{ left: at.x + 14, top: at.y + 18 }}
    >
      Open in the background
    </div>
  );
}
