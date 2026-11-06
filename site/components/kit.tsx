// The UI kit Help figures are drawn from: see docs/plans/help.md#figures.
import {
  Activity,
  Anchor,
  ArrowLeft,
  ArrowRight,
  Bookmark,
  Check,
  ChevronDown,
  ChevronRight,
  Clock,
  EllipsisVertical,
  Eye,
  FileText,
  Folder,
  Folders,
  History,
  Inbox,
  ListTree,
  Lock,
  Maximize2,
  MessageCircle,
  Minimize2,
  PanelLeftClose,
  PanelLeftOpen,
  PanelTop,
  Pause,
  Pencil,
  Play,
  Plus,
  RefreshCw,
  RotateCcw,
  RotateCw,
  Search,
  Settings,
  ShieldCheck,
  Sparkles,
  SquareArrowOutUpRight,
  SquarePlus,
  SquareX,
  Star,
  Tag,
  Terminal as TerminalIcon,
  TextSearch,
  Undo2,
  Volume2,
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

const ICONS = {
  activity: Activity,
  bookmark: Bookmark,
  chat: MessageCircle,
  check: Check,
  clock: Clock,
  close: SquareX,
  eye: Eye,
  file: FileText,
  folder: Folder,
  history: History,
  name: Sparkles,
  "new-tab": SquarePlus,
  "new-window": SquareArrowOutUpRight,
  pause: Pause,
  plus: Plus,
  refresh: RefreshCw,
  rename: Pencil,
  scratchpad: Inbox,
  search: Search,
  settings: Settings,
  shield: ShieldCheck,
  tab: PanelTop,
  tag: Tag,
  terminal: TerminalIcon,
  undo: Undo2,
};

type IconName = keyof typeof ICONS;

function Glyph({ icon }: { icon: IconName }) {
  const Icon = ICONS[icon];
  return <Icon />;
}

function Tile({ title }: { title: string }) {
  return (
    <i
      className="kit-tile"
      style={{ background: TILE_COLORS[title.charCodeAt(0) % 5] }}
    >
      {title[0]}
    </i>
  );
}

function classes(...names: (string | false | 0 | undefined)[]) {
  return names.filter(Boolean).join(" ");
}

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
  cut,
  bare,
  children,
}: {
  height?: number;
  cut?: boolean;
  bare?: boolean;
  children: ReactNode;
}) {
  return (
    <div
      className={classes("kit-window", cut && "cut", bare && "bare")}
      style={{ height }}
    >
      {children}
    </div>
  );
}

export function Sidebar({
  unread,
  floating,
  foot,
  children,
}: {
  unread?: number;
  floating?: boolean;
  foot?: boolean;
  children: ReactNode;
}) {
  return (
    <div className={classes("kit-sidebar", floating && "floating")}>
      <div className="kit-top">
        <span className="kit-lights">
          <i />
          <i />
          <i />
        </span>
        <span className="kit-button">
          {floating ? <PanelLeftOpen /> : <PanelLeftClose />}
        </span>
        <span className="kit-button kit-push">
          <Folders />
        </span>
        <span className="kit-button">
          <TextSearch />
        </span>
        <span className="kit-button">
          <Inbox />
          {unread ? <b className="kit-badge">{unread}</b> : null}
        </span>
      </div>
      {children}
      {foot && (
        <div className="kit-foot">
          <span className="kit-pill">
            <ListTree />
            Organize
          </span>
          <span className="kit-pill">
            <Plus />
            <ChevronDown />
          </span>
        </div>
      )}
    </div>
  );
}

export function Task({
  name,
  color = "blue",
  menu,
  locked,
  children,
}: {
  name: string;
  color?: TaskColor;
  menu?: boolean;
  locked?: boolean;
  children?: ReactNode;
}) {
  return (
    <>
      <div
        className={classes("kit-row kit-task", menu && "menu")}
        style={{ "--task": TASK_COLORS[color] } as CSSProperties}
      >
        <span className="kit-folder">
          <Folder className="closed" />
          <ChevronDown className="open" />
        </span>
        <span className="kit-label">{name}</span>
        {locked && <Lock className="kit-mark" />}
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
  level = 0,
  parent,
  collapsed,
  kept,
  kind,
  children,
}: {
  selected?: boolean;
  level?: number;
  parent?: boolean;
  collapsed?: number;
  kept?: boolean;
  kind?: "chat" | "terminal";
  children: string;
}) {
  return (
    <div
      className={classes(
        "kit-row kit-tab",
        selected && "on",
        (parent || collapsed) && "parent",
      )}
      style={{ marginLeft: level * 16 }}
    >
      <span className="kit-icon">
        {kind ? <Glyph icon={kind} /> : <Tile title={children} />}
        {collapsed ? <ChevronRight /> : <ChevronDown />}
      </span>
      <span className="kit-label">{children}</span>
      {kept && <Anchor className="kit-mark" />}
      {collapsed ? <span className="kit-count">{collapsed}</span> : null}
      <span className="kit-end">
        <X />
      </span>
    </div>
  );
}

// A split view in the sidebar: one box, a row per pane.
export function Split({ children }: { children: ReactNode }) {
  return <div className="kit-split">{children}</div>;
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

export function Notice({ children }: { children: string }) {
  return (
    <div className="kit-notice">
      <Check />
      <span className="kit-label">{children}</span>
      <b>Undo</b>
    </div>
  );
}

export function MiniPlayer({ time }: { time: string }) {
  return (
    <div className="kit-player">
      <span className="kit-corner left">
        <X />
      </span>
      <span className="kit-corner right">
        <Maximize2 />
      </span>
      <span className="kit-controls">
        <RotateCcw />
        <Pause />
        <RotateCw />
      </span>
      <span className="kit-time">{time}</span>
      <span className="kit-corner bottom">
        <Volume2 />
      </span>
      <i className="kit-progress" />
    </div>
  );
}

export function Page({
  address,
  eye,
  moved,
  terminal,
  move,
  children,
}: {
  address: string;
  eye?: boolean;
  moved?: boolean;
  terminal?: boolean;
  // A little window's Move button, such as "🏠 Move to Personal".
  move?: string;
  children?: ReactNode;
}) {
  return (
    <div className="kit-page">
      <div className="kit-toolbar">
        <ArrowLeft />
        <ArrowRight />
        <RotateCw />
        <span className="kit-address">
          <span className="kit-label">{address}</span>
          {eye && <Eye className="kit-eye" />}
          <Star />
        </span>
        {move && (
          <span className="kit-move">
            <span className="kit-label">{move}</span>
            <ChevronDown />
          </span>
        )}
      </div>
      <div className={classes("kit-body", terminal && "kit-shell")}>
        {children ?? (
          <div className="kit-lines">
            <i />
            <i />
            <i />
            <i />
          </div>
        )}
        {moved && (
          <div className="kit-moved">
            <b>Open here</b>
          </div>
        )}
      </div>
    </div>
  );
}

// A split view's panes in a page card, side by side or, with `down`, one
// above another. Panes nest.
export function Panes({
  down,
  children,
}: {
  down?: boolean;
  children: ReactNode;
}) {
  return <div className={classes("kit-panes", down && "down")}>{children}</div>;
}

// A pane in a split view. Panes other than the selected one show their tag,
// and a maximized pane shows its restore button.
export function Pane({
  site,
  selected,
  maximized,
  terminal,
  children,
}: {
  site: string;
  selected?: boolean;
  maximized?: boolean;
  terminal?: boolean;
  children?: ReactNode;
}) {
  return (
    <div className={classes("kit-pane", selected && "on")}>
      {terminal ? (
        <div className="kit-shell">{children}</div>
      ) : (
        (children ?? (
          <div className="kit-lines">
            <i />
            <i />
            <i />
            <i />
          </div>
        ))
      )}
      {maximized ? (
        <span className="kit-pane-tag">
          <Minimize2 />
          <X />
        </span>
      ) : (
        !selected && (
          <span className="kit-pane-tag">
            {terminal ? <Glyph icon="terminal" /> : <Tile title={site} />}
            {site}
            <Maximize2 />
            <X />
          </span>
        )
      )}
    </div>
  );
}

export function Menu({
  top,
  left,
  bottom,
  children,
}: {
  top?: number;
  left: number;
  bottom?: number;
  children: ReactNode;
}) {
  return (
    <div className="kit-menu" style={{ top, left, bottom }}>
      {children}
    </div>
  );
}

export function MenuItem({
  icon,
  keys,
  detail,
  dim,
  children,
}: {
  icon?: IconName;
  keys?: string;
  detail?: string;
  dim?: boolean;
  children: string;
}) {
  return (
    <div className={classes("kit-item", dim && "dim", detail && "tall")}>
      {icon && <Glyph icon={icon} />}
      <span className="kit-text">
        <span>{children}</span>
        {detail && <span className="kit-muted">{detail}</span>}
      </span>
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

export function Popup({
  top,
  left,
  right,
  width,
  children,
}: {
  top: number;
  left?: number;
  right?: number;
  width: number;
  children: ReactNode;
}) {
  return (
    <div className="kit-card" style={{ top, left, right, width }}>
      {children}
    </div>
  );
}

export function Title({
  icon,
  detail,
  children,
}: {
  icon?: IconName;
  detail?: string;
  children: ReactNode;
}) {
  return (
    <div className="kit-title">
      <b>
        {icon && <Glyph icon={icon} />}
        {children}
      </b>
      {detail && <span>{detail}</span>}
    </div>
  );
}

export function Section({
  title,
  count,
  link,
  children,
}: {
  title: string;
  count?: number;
  link?: string;
  children: ReactNode;
}) {
  return (
    <div className="kit-section">
      <div className="kit-section-head">
        <b>{title}</b>
        {count !== undefined && <span>{count}</span>}
        {link && <em>{link} ›</em>}
      </div>
      <div className="kit-list">{children}</div>
    </div>
  );
}

export function Row({
  icon,
  tile,
  title,
  detail,
  retry,
  selected,
  children,
}: {
  icon?: IconName;
  tile?: string;
  title: ReactNode;
  detail?: ReactNode;
  // An Inbox alert's Retry, under its text.
  retry?: boolean;
  selected?: boolean;
  children?: ReactNode;
}) {
  return (
    <div className={classes("kit-line-row", selected && "on")}>
      {icon && <Glyph icon={icon} />}
      {tile && <Tile title={tile} />}
      <span className="kit-text">
        <b>{title}</b>
        {detail && <span>{detail}</span>}
        {retry && (
          <em className="kit-retry">
            <Glyph icon="refresh" />
            Retry
          </em>
        )}
      </span>
      {children && <span className="kit-trail">{children}</span>}
    </div>
  );
}

export function Muted({ children }: { children: ReactNode }) {
  return <span className="kit-muted">{children}</span>;
}

export function Accent({
  tone,
  children,
}: {
  tone?: "red";
  children: ReactNode;
}) {
  return <span className={classes("kit-accent", tone)}>{children}</span>;
}

export function Dot() {
  return <i className="kit-dot" />;
}

export function Button({
  primary,
  icon,
  children,
}: {
  primary?: boolean;
  icon?: IconName;
  children: ReactNode;
}) {
  return (
    <span className={classes("kit-btn", primary && "primary")}>
      {icon && <Glyph icon={icon} />}
      {children}
    </span>
  );
}

export function Buttons({ children }: { children: ReactNode }) {
  return <div className="kit-buttons">{children}</div>;
}

export function Chip({
  icon,
  on,
  children,
}: {
  icon?: IconName;
  on?: boolean;
  children: ReactNode;
}) {
  return (
    <span className={classes("kit-chip", on && "on")}>
      {icon && <Glyph icon={icon} />}
      {children}
    </span>
  );
}

export function Chips({ children }: { children: ReactNode }) {
  return <div className="kit-chips">{children}</div>;
}

export function Switch({ on }: { on?: boolean }) {
  return <i className={classes("kit-switch", on && "on")} />;
}

export function Field({
  icon,
  value,
  placeholder,
}: {
  icon?: IconName;
  value?: string;
  placeholder?: string;
}) {
  return (
    <div className="kit-field">
      {icon && <Glyph icon={icon} />}
      {value ? (
        <span>
          {value}
          <i className="kit-caret" />
        </span>
      ) : (
        <span className="kit-muted">{placeholder}</span>
      )}
    </div>
  );
}

export function Segmented({
  options,
  value,
}: {
  options: string;
  value: string;
}) {
  return (
    <div className="kit-segmented">
      {options.split(",").map((option) => (
        <span key={option} className={option === value ? "on" : undefined}>
          {option}
        </span>
      ))}
    </div>
  );
}

export function Slider({ label, value }: { label: string; value: number }) {
  return (
    <div className="kit-slider">
      <span>{label}</span>
      <i style={{ "--value": `${value}%` } as CSSProperties} />
    </div>
  );
}

export function ColorField() {
  return (
    <div className="kit-color-field">
      <i style={{ left: "62%", top: "28%" }} />
      <i style={{ left: "78%", top: "36%" }} />
      <b>
        <Plus />
      </b>
    </div>
  );
}

export function Swatches() {
  return (
    <div className="kit-swatches">
      {[
        "#e4dff5",
        "#dbe6fb",
        "#d7efe2",
        "#f6e39b",
        "#f8e2d4",
        "#5a1f3d",
        "#1f3a2e",
      ].map((color, index) => (
        <i
          key={color}
          className={index === 0 ? "on" : undefined}
          style={{ background: color }}
        />
      ))}
    </div>
  );
}

export function Chat({
  title,
  status,
  waiting,
  children,
}: {
  title: string;
  status?: string;
  waiting?: boolean;
  children: ReactNode;
}) {
  return (
    <div className="kit-chat">
      <div className="kit-chat-head">
        <b>{title}</b>
        {status && (
          <span className={classes("kit-status", waiting && "waiting")}>
            <i />
            {status}
          </span>
        )}
      </div>
      {children}
    </div>
  );
}

export function Message({ children }: { children: ReactNode }) {
  return <div className="kit-message">{children}</div>;
}

export function Answer({ children }: { children: ReactNode }) {
  return <div className="kit-answer">{children}</div>;
}

export function Step({
  icon = "file",
  children,
}: {
  icon?: IconName;
  children: ReactNode;
}) {
  return (
    <div className="kit-step">
      <Glyph icon={icon} />
      <span>{children}</span>
    </div>
  );
}

export function Approval({
  title,
  children,
}: {
  title: string;
  children: ReactNode;
}) {
  return (
    <div className="kit-approval">
      <b>
        <ShieldCheck />
        {title}
      </b>
      {children}
    </div>
  );
}

export function Composer({
  placeholder,
  children,
}: {
  placeholder: string;
  children?: ReactNode;
}) {
  return (
    <div className="kit-composer">
      <span className="kit-muted">{placeholder}</span>
      <div className="kit-composer-foot">
        {children}
        <b className="kit-send">
          <Play />
        </b>
      </div>
    </div>
  );
}

export function Terminal({
  title,
  children,
}: {
  title: string;
  children: ReactNode;
}) {
  return (
    <div className="kit-terminal">
      <div className="kit-terminal-top">
        <span className="kit-lights">
          <i />
          <i />
          <i />
        </span>
        <span>{title}</span>
      </div>
      <div className="kit-shell">{children}</div>
    </div>
  );
}

export function Cmd({ children }: { children: string }) {
  return (
    <div className="kit-cmd">
      <span>$</span> {children}
    </div>
  );
}

export function Out({
  tone,
  children,
}: {
  tone?: "dim" | "ok" | "warn" | "accent";
  children: string;
}) {
  return <div className={classes("kit-out", tone)}>{children}</div>;
}

export function NewTabBox() {
  return (
    <div className="kit-ntp-box">
      <Search />
      <span className="kit-muted">Search or type an address</span>
      <span className="kit-segmented small">
        <span className="on">Search</span>
        <span>Ask</span>
      </span>
    </div>
  );
}

export function Columns({ children }: { children: ReactNode }) {
  return <div className="kit-columns">{children}</div>;
}

export function Stack({ children }: { children: ReactNode }) {
  return <div className="kit-stack">{children}</div>;
}
