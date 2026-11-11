import { useEffect, useState } from "react";

// The time, every `every` milliseconds while `on`.
export function useNow(on: boolean, every = 1000): number {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (!on) {
      return;
    }
    const id = setInterval(() => setNow(Date.now()), every);
    return () => clearInterval(id);
  }, [on, every]);
  return now;
}
