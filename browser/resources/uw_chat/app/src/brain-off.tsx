import type { IconComponentProps } from "@/lib/icon-context";
import { cn } from "@/lib/utils";

// Lucide's brain-off, which lucide-react does not have yet.
export function BrainOff({ className, strokeWidth = 2 }: IconComponentProps) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
      className={cn("size-4", className)}
    >
      <path d="M12 18v-6m0-7v1.344m5.707-.051A3 3 0 1 0 12 5a3 3 0 0 0-4.124-2.781" />
      <path d="M17.997 5.125a4 4 0 0 1 2.526 5.77m-.879 8.749A4 4 0 0 1 12 18a4 4 0 1 1-7.967-.517M2 2l20 20" />
      <path d="M20 10.536a4 4 0 0 1 1.533 5.34M3.477 10.895a4 4 0 0 1 1.876-5.542" />
      <path d="M6 18a4 4 0 0 1-2-7.464M9 13a4.17 4.17 0 0 0 2.166-1.5a4 4 0 0 0 .138-.196" />
    </svg>
  );
}
