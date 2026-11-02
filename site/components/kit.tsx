// The UI kit Help figures are drawn from: see docs/plans/help.md#figures.
import {
  ArrowLeft,
  ArrowRight,
  ChevronDown,
  EllipsisVertical,
  Folder,
  Inbox,
  PanelLeft,
  Pencil,
  RotateCw,
  Sparkles,
  SquareArrowOutUpRight,
  SquarePlus,
  SquareX,
  X,
} from "lucide-react";
import type { CSSProperties, ReactNode } from "react";
import "./kit.css";

const TASK_COLORS = {
  grey: "#5f6368",
  blue: "#1a73e8",
  red: "#d93025",
  yellow: "#e8a200",
  green: "#188038",
  pink: "#d01884",
  purple: "#a142f4",
  cyan: "#007b83",
  orange: "#e8710a",
};

type TaskColor = keyof typeof TASK_COLORS;

const TILE_COLORS = ["#6e56cf", "#1a73e8", "#188038", "#e8710a", "#d01884"];

const MENU_ICONS = {
  rename: Pencil,
  name: Sparkles,
  "new-tab": SquarePlus,
  "new-window": SquareArrowOutUpRight,
  scratchpad: Inbox,
  close: SquareX,
};

export function Figure({
  caption,
  children,
}: {
  caption: string;
  children: ReactNode;
}) {
  return (
    <figure className="kit-figure not-prose">
      <div className="kit" aria-hidden="true">
        {children}
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

export function Window({
  height,
  children,
}: {
  height?: number;
  children: ReactNode;
}) {
  return (
    <div
      className={height ? "kit-window cut" : "kit-window"}
      style={{ height }}
    >
      {children}
    </div>
  );
}

export function Sidebar({ children }: { children: ReactNode }) {
  return (
    <div className="kit-sidebar">
      <div className="kit-top">
        <span className="kit-lights">
          <i />
          <i />
          <i />
        </span>
        <span className="kit-button">
          <PanelLeft />
        </span>
        <span className="kit-button">
          <Inbox />
        </span>
      </div>
      {children}
    </div>
  );
}

export function Task({
  name,
  color = "blue",
  menu,
  children,
}: {
  name: string;
  color?: TaskColor;
  menu?: boolean;
  children?: ReactNode;
}) {
  return (
    <>
      <div
        className={menu ? "kit-row kit-task menu" : "kit-row kit-task"}
        style={{ "--task": TASK_COLORS[color] } as CSSProperties}
      >
        <span className="kit-folder">
          <Folder className="closed" />
          <ChevronDown className="open" />
        </span>
        <span className="kit-label">{name}</span>
        <span className="kit-end">
          <EllipsisVertical />
          <X />
        </span>
      </div>
      {children && <div className="kit-in">{children}</div>}
    </>
  );
}

export function TabRow({
  selected,
  children,
}: {
  selected?: boolean;
  children: string;
}) {
  const tile = TILE_COLORS[children.charCodeAt(0) % TILE_COLORS.length];
  return (
    <div className={selected ? "kit-row kit-tab on" : "kit-row kit-tab"}>
      <i className="kit-tile" style={{ background: tile }}>
        {children[0]}
      </i>
      <span className="kit-label">{children}</span>
      <span className="kit-end">
        <X />
      </span>
    </div>
  );
}

export function Scratchpad({ children }: { children: ReactNode }) {
  return (
    <>
      <div className="kit-heading">
        <span className="kit-label">Scratchpad</span>
        <span className="kit-end">
          <b>Tidy</b>
          <b>Clear</b>
        </span>
      </div>
      {children}
    </>
  );
}

export function Page({ address }: { address: string }) {
  return (
    <div className="kit-page">
      <div className="kit-toolbar">
        <ArrowLeft />
        <ArrowRight />
        <RotateCw />
        <span className="kit-address">{address}</span>
      </div>
      <div className="kit-body">
        <i />
        <i />
        <i />
        <i />
      </div>
    </div>
  );
}

export function Menu({
  top,
  left,
  children,
}: {
  top: number;
  left: number;
  children: ReactNode;
}) {
  return (
    <div className="kit-menu" style={{ top, left }}>
      {children}
    </div>
  );
}

export function MenuItem({
  icon,
  keys,
  children,
}: {
  icon: keyof typeof MENU_ICONS;
  keys?: string;
  children: string;
}) {
  const Icon = MENU_ICONS[icon];
  return (
    <div className="kit-item">
      <Icon />
      <span className="kit-label">{children}</span>
      {keys && <span className="kit-keys">{keys}</span>}
    </div>
  );
}

export function MenuColors({ current }: { current: TaskColor }) {
  return (
    <div className="kit-colors">
      {Object.entries(TASK_COLORS).map(([name, color]) => (
        <i
          key={name}
          className={name === current ? "on" : undefined}
          style={{ "--dot": color } as CSSProperties}
        />
      ))}
    </div>
  );
}

export function MenuLine() {
  return <hr className="kit-separator" />;
}
