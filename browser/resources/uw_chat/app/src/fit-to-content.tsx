// A pop-up's page that the pop-up sizes to, such as the Inbox: its height
// slides to its content's whenever the content changes, so the pop-up grows
// and shrinks with it, and a new view fades in over the one it replaces. See
// the canvas guide uw-scheduled-page-and-inbox, "The pop-up".
import { AnimatePresence, motion, useReducedMotion } from "motion/react";
import { useLayoutEffect, useRef, useState, type ReactNode } from "react";
import { cn } from "@/lib/utils";

export const SLIDE = { duration: 0.2, ease: "easeOut" } as const;

export function FitToContent({ children }: { children: ReactNode }) {
  const content = useRef<HTMLDivElement>(null);
  const [height, setHeight] = useState<number>();
  // Height is no transform, so MotionConfig's reduced motion leaves it be.
  const reduceMotion = useReducedMotion() ?? false;
  useLayoutEffect(() => {
    const element = content.current;
    if (!element) {
      return;
    }
    const observer = new ResizeObserver(() =>
      setHeight(element.offsetHeight),
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  return (
    <motion.div
      className="overflow-hidden"
      initial={false}
      animate={height === undefined ? undefined : { height }}
      transition={reduceMotion ? { duration: 0 } : SLIDE}
    >
      <div ref={content}>{children}</div>
    </motion.div>
  );
}

// `children` fading in over the view they replace, which had another `view`.
// A flex column, so a list inside can shrink and scroll; `className` adds to
// it, such as a gap.
export function FadeOver({
  view,
  className,
  children,
}: {
  view: string;
  className?: string;
  children: ReactNode;
}) {
  return (
    <div className="relative flex min-h-0 flex-col">
      <AnimatePresence initial={false} mode="popLayout">
        <motion.div
          key={view}
          className={cn("flex min-h-0 flex-col", className)}
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          exit={{ opacity: 0 }}
          transition={SLIDE}
        >
          {children}
        </motion.div>
      </AnimatePresence>
    </div>
  );
}
