import Image from "next/image";
import type { CSSProperties } from "react";
import { Brew } from "./brew";
import { Dock } from "./dock";
import { Film } from "./film";
import { Filmstrip, type Stop } from "./filmstrip";
import { Favicon } from "./icons";
import { Sidebar, type SidebarState } from "./sidebar";
import { bike, launch, moved, sorted, tab, trip } from "./tabs";
import "./home.css";

const DOWNLOAD =
  "https://github.com/leetdavid/uw/releases/latest/download/uw-macos-arm64.dmg";
const REPO = "https://github.com/leetdavid/uw";

const working: SidebarState = {
  tasks: [
    { ...trip, tabs: [tab.guide, tab.flight, tab.walk] },
    {
      ...launch("passed"),
      tabs: [
        {
          id: "blockers",
          title: "Launch blockers",
          icon: { site: "linear.app" },
        },
        tab.checklist,
        { ...tab.run, title: "Run #4821 passed" },
      ],
    },
    { ...bike("Buy a road bike"), tabs: [] },
  ],
  scratchpad: [tab.news],
  active: "blockers",
};

const docked: SidebarState = {
  tasks: [
    {
      ...trip,
      tabs: [
        {
          id: "alfama",
          title: "Sunlit flat in Alfama",
          icon: { site: "booking.com" },
        },
        tab.walk,
      ],
    },
  ],
  scratchpad: [],
  active: "alfama",
};

const stops: Stop[] = [
  {
    title: "Go down every rabbit hole",
    text: "Every new tab starts in the Scratchpad.",
    sidebar: {
      tasks: [],
      scratchpad: [
        tab.guide,
        tab.checklist,
        tab.flight,
        tab.strada,
        tab.size,
        tab.run,
        tab.flat,
        tab.news,
      ],
      active: "flat",
    },
  },
  {
    title: "Watch it sort itself",
    text: "Each tab files into its task as you open it.",
    sidebar: {
      tasks: [trip, launch("failed")],
      scratchpad: [tab.strada],
      active: "nata",
      notice: moved,
    },
  },
  {
    title: "New idea? New task.",
    text: "Something new gets a home of its own.",
    sidebar: {
      tasks: [
        { ...trip, tabs: [] },
        { ...launch("failed"), tabs: [] },
        bike("New task", true),
      ],
      scratchpad: [],
      active: "strada",
      notice: {
        title: "Naming the task",
        detail: "Asking Llama 3.2",
        busy: true,
      },
    },
  },
  {
    title: "Change your mind? One click.",
    text: "Every move can be undone.",
    sidebar: {
      ...sorted,
      tasks: sorted.tasks.map((task) => ({ ...task, tabs: [] })),
    },
  },
];

function accent(color: string) {
  return { "--g": color } as CSSProperties;
}

function DownloadIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M12 4v11" />
      <path d="m7 10 5 5 5-5" />
      <path d="M5 20h14" />
    </svg>
  );
}

function Download() {
  return (
    <a className="btn" href={DOWNLOAD}>
      <DownloadIcon />
      Download for Mac
    </a>
  );
}

function Logo({ size }: { size: number }) {
  return <Image src="/logo.svg" alt="" width={size} height={size} />;
}

function HeroWindow() {
  return (
    <div className="win">
      <Sidebar
        state={working}
        label="uw's sidebar: three tasks, the Scratchpad, and a video docked at its foot"
      >
        <Dock />
      </Sidebar>
      <div className="page">
        <div className="toolbar">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <path d="m12 19-7-7 7-7M19 12H5" />
          </svg>
          <div className="omni">
            <Logo size={14} />
            Chat · Ship the launch
          </div>
        </div>
        <div className="chat">
          <div className="me">
            Which Linear issues block the launch? Close the tabs I&apos;m done
            with.
          </div>
          <div className="bot">
            Three issues block it, two assigned to you. I read <b>6 tabs</b> in
            Ship the launch and found <b>5</b> you haven&apos;t touched since
            you merged.
          </div>
          <div className="wait">
            <small>WAITING FOR YOU</small>
            <b>Close 5 tabs in Ship the launch?</b>
            <div className="btns">
              <span className="yes">Close 5 tabs</span>
              <span>Keep them</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default function Home() {
  return (
    <main className="home">
      <section className="hero">
        <div className="stars" />
        <nav className="nav">
          <a className="brand" href="/">
            <Logo size={30} />
            uw
          </a>
          <div className="links">
            <a href="#how">How it works</a>
            <a href="#features">Features</a>
            <a href="/help">Help</a>
            <a href={REPO}>GitHub</a>
            <a className="get" href={DOWNLOAD}>
              Download
            </a>
          </div>
        </nav>
        <div className="copy">
          <span className="pill">
            <b>v0.1</b>Early preview for Mac is here
          </span>
          <h1>
            <span>ur web,</span> <em className="glow">ur way</em>
          </h1>
          <p className="lead">
            The browser that puts your tabs away for you. uw sorts them into
            tasks on its own, docks your video in the sidebar, and brings an
            agent that asks before it acts.
          </p>
          <div className="cta">
            <Download />
            <Brew />
          </div>
          <p className="meta">Apple silicon · macOS 13 or later</p>
        </div>
        <Film download={DOWNLOAD}>
          <HeroWindow />
        </Film>
        <div className="fade" />
      </section>

      <section className="how" id="how">
        <div className="intro">
          <div>
            <span className="kick">How it works</span>
            <h2>
              Open everything.
              <br />
              <em className="glow">It settles on its own.</em>
            </h2>
          </div>
          <p>
            Every tab drifts into the task it belongs to the moment you open it.
            Nothing to file, nothing to drag. Change your mind, and Undo puts it
            back.
          </p>
        </div>
        <Filmstrip stops={stops} />
      </section>

      <section className="feat" id="features">
        <div className="head">
          <div>
            <span className="kick">Everything else</span>
            <h2>
              Built in, <em className="glow">not bolted on.</em>
            </h2>
          </div>
          <p>
            The rest of what makes uw yours, from the video in your sidebar to
            an agent in your terminal.
          </p>
        </div>
        <div className="bento">
          <div className="tile s7" style={accent("#FF7AC6")}>
            <small>Mini player</small>
            <h3>
              Your video, <em>docked in the sidebar.</em>
            </h3>
            <p>
              Picture in picture becomes part of the sidebar, right under your
              tabs. No window to drag around.
            </p>
            <div className="vis">
              <div className="mp">
                <Sidebar
                  state={docked}
                  label="A video docked in uw's sidebar, under the tabs"
                >
                  <Dock />
                </Sidebar>
              </div>
              <span className="chip pink">
                <svg
                  viewBox="0 0 24 24"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="2.2"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  aria-hidden="true"
                >
                  <path d="M19 12H5m6-6-6 6 6 6" />
                </svg>
                Docked under your tabs
              </span>
            </div>
          </div>

          <div className="tile s5" style={accent("#5CD6F2")}>
            <small>Command bar</small>
            <h3>
              Type anything. <em>Get anywhere.</em>
            </h3>
            <p>Pages, tabs, math, currencies and time zones in one bar.</p>
            <div className="vis">
              <div className="ui cmd">
                <div className="field">
                  <svg
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                    strokeLinecap="round"
                    aria-hidden="true"
                  >
                    <circle cx="11" cy="11" r="7" />
                    <path d="m20 20-3.5-3.5" />
                  </svg>
                  180k usd to hkd
                  <i />
                </div>
                <div className="sum">
                  <b>HK$1,401,840</b>
                  <small>180,000 US dollars · rate at 9:41</small>
                </div>
                <div className="hints">
                  <span>Return to copy</span>
                  <span>Cmd Return to ask uw</span>
                </div>
              </div>
            </div>
          </div>

          <div className="tile s5" style={accent("#B9A6FF")}>
            <small>Agent</small>
            <h3>
              An agent that <em>asks first.</em>
            </h3>
            <p>
              Pick any model, from Claude to one running on your Mac. It reads,
              compares, and waits for your yes.
            </p>
            <div className="vis">
              <div className="ui chat">
                <div className="me">
                  Keep the best three flats under €1,500.
                </div>
                <div className="bot">
                  I read 9 listings in <b>Plan Lisbon trip</b>. Three fit.
                </div>
                <div className="wait">
                  <small>WAITING FOR YOU</small>
                  <b>Close the 6 that don&apos;t fit?</b>
                  <div className="btns">
                    <span className="yes">Close 6 tabs</span>
                    <span>Keep them</span>
                  </div>
                </div>
              </div>
            </div>
          </div>

          <div className="tile s7" style={accent("#7FE3F7")}>
            <small>Workspaces and themes</small>
            <h3>
              A space for every <em>part of your life.</em>
            </h3>
            <p>
              Swipe between Work and Home. Each keeps its own tabs and its own
              colors.
            </p>
            <div className="vis">
              <div className="ui space leisure">
                <div className="row title">🏡 Weekend in Sintra</div>
                <div className="row on">
                  <Favicon domain="booking.com" size={14} />
                  Quinta da Regaleira
                </div>
              </div>
              <div className="ui space studio">
                <div className="row title">🎨 Album artwork</div>
                <div className="row on">
                  <Favicon domain="figma.com" size={14} />
                  Cover, round 3
                </div>
                <div className="row">
                  <Favicon domain="spotify.com" size={14} />
                  Reference playlist
                </div>
              </div>
              <div className="ui space office">
                <div className="row title">💼 Q4 launch review</div>
                <div className="row on">
                  <Favicon domain="notion.so" size={14} />
                  Launch checklist
                </div>
              </div>
            </div>
          </div>

          <div className="tile s4" style={accent("#FFB38A")}>
            <small>Watch this page</small>
            <h3>
              It keeps <em>an eye out.</em>
            </h3>
            <p>Watch a price or a page. uw pings you when it moves.</p>
            <div className="vis short">
              <div className="ui watch">
                <b>SFO to Lisbon, May 14</b>
                <div className="price">
                  <strong>$548</strong>
                  <span>−$84</span>
                </div>
                <svg
                  viewBox="0 0 300 50"
                  preserveAspectRatio="none"
                  aria-hidden="true"
                >
                  <path d="M0 12 L50 10 L100 18 L150 14 L200 28 L250 34 L300 44" />
                </svg>
              </div>
            </div>
          </div>

          <div className="tile s4" style={accent("#81C995")}>
            <small>Terminal</small>
            <h3>
              Lives in your <em>terminal too.</em>
            </h3>
            <p>A real shell in every task, and a uw command for your agents.</p>
            <div className="vis short">
              <pre className="ui term">
                <span className="p">~ $</span> uw chat &quot;what&apos;s
                left?&quot;
                {"\n"}
                <span className="ok">●</span> 3 blockers, 1 failing check.
                {"\n"}
                <span className="p">~ $</span> <i />
              </pre>
            </div>
          </div>

          <div className="tile s4" style={accent("#FF9AD5")}>
            <small>Private by default</small>
            <h3>
              Your AI, <em>your key.</em>
            </h3>
            <p>
              OpenAI, Anthropic, or a model on your own Mac. Nothing is reported
              unless you say yes.
            </p>
            <div className="vis short models">
              <span className="chip">
                <Favicon domain="anthropic.com" size={16} />
                Claude
              </span>
              <span className="chip">
                <Favicon domain="openai.com" size={16} />
                GPT
              </span>
              <span className="chip">
                <Favicon domain="ollama.com" size={16} />
                Ollama
              </span>
            </div>
          </div>
        </div>
      </section>

      <section className="end">
        <Image
          className="logo"
          src="/logo.svg"
          alt="uw logo"
          width={104}
          height={104}
        />
        <h2>
          Your tabs have been
          <br />
          <em className="glow">waiting for this.</em>
        </h2>
        <p>
          uw is an early preview: new, a little rough, and better every week.
          Try it and tell us what you think.
        </p>
        <div className="cta">
          <Download />
          <Brew />
        </div>
        <div className="horizon" />
        <footer>
          <a className="brand" href="/">
            <Logo size={26} />
            uw
          </a>
          <span>© 2026 uw. Built on Chromium.</span>
          <nav>
            <a href={`${REPO}/releases`}>Releases</a>
            <a href={`${REPO}/issues/new`}>Report an issue</a>
            <a href={REPO}>Source</a>
          </nav>
        </footer>
      </section>
    </main>
  );
}
