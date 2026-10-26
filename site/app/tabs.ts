import type { Notice, SidebarState, Tab, Task } from "./sidebar";

const cadence = { letter: "C", background: "#111" };

export const tab = {
  guide: {
    id: "guide",
    title: "Four days in Lisbon",
    icon: { site: "lonelyplanet.com" },
  },
  checklist: {
    id: "checklist",
    title: "Launch checklist",
    icon: { site: "notion.so" },
  },
  flight: {
    id: "flight",
    title: "SFO to Lisbon, May 14",
    icon: { site: "kayak.com" },
  },
  strada: { id: "strada", title: "Strada AL, gravel", icon: cadence },
  size: { id: "size", title: "Find your size", icon: cadence, child: true },
  run: { id: "run", title: "Run #4821 failed", icon: { site: "github.com" } },
  flat: {
    id: "flat",
    title: "Sunlit flat with a balcony",
    icon: { letter: "s", background: "#0F766E" },
  },
  nata: {
    id: "nata",
    title: "The best pastel de nata in Lisbon",
    icon: { letter: "B", background: "#2A1A0E", color: "#F6A15A" },
  },
  news: {
    id: "news",
    title: "Hacker News",
    icon: { site: "news.ycombinator.com" },
  },
  walk: { id: "walk", title: "Lisbon 4K walk", icon: { site: "youtube.com" } },
} satisfies Record<string, Tab>;

export const trip: Task = {
  id: "trip",
  name: "Plan Lisbon trip",
  color: "#1A73E8",
  tabs: [tab.guide, tab.flight, tab.flat, tab.nata],
};

export function launch(run: string): Task {
  return {
    id: "launch",
    name: "Ship the launch",
    color: "#188038",
    tabs: [tab.checklist, { ...tab.run, title: `Run #4821 ${run}` }],
  };
}

export function bike(name: string, naming = false): Task {
  return {
    id: "bike",
    name,
    color: "#D01884",
    naming,
    tabs: [tab.strada, tab.size],
  };
}

export const moved: Notice = {
  title: "Moved to Plan Lisbon trip",
  detail: "The best pastel de nata in Lisbon",
};

export const sorted: SidebarState = {
  tasks: [trip, launch("passed"), bike("Buy a road bike")],
  scratchpad: [tab.news],
  active: "nata",
  notice: moved,
};
