// How every page starts, and what the two pop-ups share.
import { StrictMode, useEffect, useRef, type ReactNode } from "react";
import { createRoot } from "react-dom/client";
import { MotionConfig } from "motion/react";

// Draws `page` in the document's root element.
export function mount(page: ReactNode) {
  // Fluid's parts read dark mode from a class on the root element.
  const dark = matchMedia("(prefers-color-scheme: dark)");
  const followScheme = () =>
    document.documentElement.classList.toggle("dark", dark.matches);
  followScheme();
  dark.addEventListener("change", followScheme);

  createRoot(document.getElementById("root")!).render(
    <StrictMode>
      <MotionConfig reducedMotion="user">{page}</MotionConfig>
    </StrictMode>,
  );
}

// A pop-up takes the frosted look when the browser says so, and asks to be
// shown once, when it first has something to draw.
export function usePopUp(ready: boolean, frosted: boolean, showUI: () => void) {
  const shown = useRef(false);
  useEffect(() => {
    document.documentElement.classList.toggle("frosted", frosted);
  }, [frosted]);
  useEffect(() => {
    if (ready && !shown.current) {
      shown.current = true;
      showUI();
    }
  }, [ready]);
}
