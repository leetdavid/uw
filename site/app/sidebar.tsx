import { Fragment, type ReactNode } from "react";
import { Favicon, Folder, Line } from "./icons";

type Icon =
  | { site: string }
  | { letter: string; background: string; color?: string };

export interface Tab {
  id: string;
  title: string;
  icon: Icon;
  child?: boolean;
}

export interface Task {
  id: string;
  name: string;
  color: string;
  naming?: boolean;
  tabs: Tab[];
}

export interface Notice {
  title: string;
  detail: string;
  busy?: boolean;
}

export interface SidebarState {
  tasks: Task[];
  scratchpad: Tab[];
  active: string;
  notice?: Notice;
}

const FAVORITES = [
  "slack.com",
  "calendar.google.com",
  "linear.app",
  "spotify.com",
];

function TabIcon({ icon }: { icon: Icon }) {
  if ("site" in icon) return <Favicon domain={icon.site} size={16} />;
  return (
    <i
      className="badge"
      style={{ background: icon.background, color: icon.color }}
    >
      {icon.letter}
    </i>
  );
}

function NoticeCard({ notice }: { notice: Notice }) {
  return (
    <div className="notice">
      <b>
        {notice.busy ? (
          <i className="spin" />
        ) : (
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <circle cx="12" cy="12" r="10" fill="#1E1B2E" />
            <path
              d="m8 12 3 3 5-6"
              fill="none"
              stroke="#fff"
              strokeWidth="2.2"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          </svg>
        )}
        {notice.title}
      </b>
      <span>{notice.detail}</span>
      {notice.busy ? (
        <div className="meter">
          <i />
        </div>
      ) : (
        <span className="undo">Undo</span>
      )}
    </div>
  );
}

export function Sidebar({
  state,
  label,
  children,
}: {
  state: SidebarState;
  label: string;
  children?: ReactNode;
}) {
  const row = (tab: Tab) => {
    const active = tab.id === state.active;
    return (
      <div
        key={tab.id}
        className={["tab", tab.child && "child", active && "on"]
          .filter(Boolean)
          .join(" ")}
      >
        <TabIcon icon={tab.icon} />
        <span>{tab.title}</span>
        {active && (
          <Line className="close">
            <path d="M18 6 6 18M6 6l12 12" />
          </Line>
        )}
      </div>
    );
  };

  return (
    <div className="side" role="img" aria-label={label}>
      <div className="top">
        <div className="lights">
          <i />
          <i />
          <i />
        </div>
        <Line>
          <rect width="18" height="18" x="3" y="3" rx="2" />
          <path d="M9 3v18" />
        </Line>
      </div>
      <div className="favs">
        {FAVORITES.map((site) => (
          <div key={site}>
            <Favicon domain={site} size={18} />
          </div>
        ))}
      </div>
      {state.tasks.map((task) => (
        <Fragment key={task.id}>
          <div className="task">
            <Folder color={task.color} />
            <span>{task.name}</span>
            {task.naming && <i className="spin" />}
          </div>
          <div className="in">{task.tabs.map(row)}</div>
        </Fragment>
      ))}
      {state.scratchpad.length > 0 && <div className="label">Scratchpad</div>}
      {state.scratchpad.map(row)}
      {state.notice && <NoticeCard notice={state.notice} />}
      {children}
      <div className="foot">
        <span className="organize">
          <Line>
            <path d="M21 12h-8M21 6H8M21 18h-8M3 6v4c0 1.1.9 2 2 2h3M3 10v6c0 1.1.9 2 2 2h3" />
          </Line>
        </span>
        <span className="new">
          <b>
            <Line>
              <path d="M5 12h14M12 5v14" />
            </Line>
          </b>
          <i />
          <b>
            <Line>
              <path d="m6 9 6 6 6-6" />
            </Line>
          </b>
        </span>
      </div>
    </div>
  );
}
