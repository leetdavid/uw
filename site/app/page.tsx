import Image from "next/image";
import poster from "@/assets/poster.png";
import stop1 from "@/assets/stop-1.png";
import stop2 from "@/assets/stop-2.png";
import stop3 from "@/assets/stop-3.png";
import stop4 from "@/assets/stop-4.png";
import tram from "@/assets/tram.jpg";
import { ScrollDemo, type Stop } from "./scroll-demo";

const DOWNLOAD =
  "https://github.com/leetdavid/uw/releases/latest/download/uw-macos-arm64.dmg";
const REPO = "https://github.com/leetdavid/uw";

const stops: Stop[] = [
  {
    title: (
      <>
        Go down every <em>rabbit hole.</em>
      </>
    ),
    label: "Go down every rabbit hole",
    text: "Flights, a flat, a launch, a bike you keep not buying. Open it all. Every new tab starts in the Scratchpad, as in any browser.",
    image: stop1,
    alt: "uw's sidebar with nine tabs from different kinds of work in the Scratchpad",
  },
  {
    title: (
      <>
        Watch it <em>sort itself.</em>
      </>
    ),
    label: "Watch it sort itself",
    text: "Every tab lands in the task it belongs to, the moment you open it. No folders to make, nothing to drag, and Undo is always one click away.",
    image: stop2,
    alt: "The tabs filed into Plan Lisbon trip and Ship the launch, with a card that says Moved to Plan Lisbon trip and offers Undo",
  },
  {
    title: (
      <>
        New idea? <em>New task.</em>
      </>
    ),
    label: "New idea? New task.",
    text: "Start something new and uw gives it a home of its own, named once it has read the pages. A name you type is never replaced.",
    image: stop3,
    alt: "Two bike pages forming a new task while uw names it",
  },
  {
    title: (
      <>
        Change your mind? <em>One click.</em>
      </>
    ),
    label: "Change your mind? One click.",
    text: "Every move uw makes can be undone. Put a tab somewhere yourself, and Keep Here holds it there.",
    image: stop4,
    alt: "Three tasks in a calm sidebar, with Undo on the last move",
  },
];

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

function Folder({ color }: { color: string }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill={`color-mix(in srgb, ${color} 24%, transparent)`}
      stroke={color}
      strokeWidth="1.75"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z" />
    </svg>
  );
}

function Favicon({ domain, size = 17 }: { domain: string; size?: number }) {
  return (
    <Image src={`/icons/${domain}.png`} alt="" width={size} height={size} />
  );
}

function Download() {
  return (
    <a className="dl" href={DOWNLOAD}>
      <DownloadIcon />
      Download for Mac
    </a>
  );
}

export default function Home() {
  return (
    <main>
      <section className="hero">
        <nav className="nav">
          <a className="brand" href="/">
            <Image
              src="/logo.svg"
              alt=""
              width={30}
              height={30}
              style={{ margin: -2 }}
            />
            uw
          </a>
          <div className="links">
            <a href="#how">How it works</a>
            <a href="#features">Features</a>
            <a href={REPO}>GitHub</a>
            <a className="get" href={DOWNLOAD}>
              Download
            </a>
          </div>
        </nav>
        <div className="copy">
          <span className="eyebrow">
            <i />
            Now in early preview for Mac
          </span>
          <h1 className="serif">
            ur web, <em>ur way</em>
          </h1>
          <p className="lead">
            The calm browser for people with too many tabs. uw sorts them into
            tasks on its own, keeps your video playing in the sidebar, and
            brings an agent that asks before it acts.
          </p>
          <div className="actions">
            <Download />
            <a className="src" href="#how">
              See how it works &darr;
            </a>
          </div>
          <p className="meta">Apple silicon &middot; macOS 13 or later</p>
        </div>
        <div className="player">
          <Image
            src={poster}
            alt="Meet uw: a glass sidebar of tasks with a video docked at its foot"
            sizes="(max-width: 1080px) 100vw, 1040px"
            preload
          />
        </div>
        <div className="curve" />
      </section>

      <section className="intro" id="how">
        <div className="kicker">How it works</div>
        <h2 className="serif">
          Less tab housekeeping.
          <br />
          <em>More of what you opened them for.</em>
        </h2>
        <p>Scroll, and watch a messy afternoon put itself away.</p>
      </section>
      <ScrollDemo stops={stops} />

      <section className="feat" id="features">
        <div className="head">
          <div className="kicker">Everything else</div>
          <h2 className="serif">
            Small things that <em>add up to calm.</em>
          </h2>
          <p>The rest of what makes uw feel like yours.</p>
        </div>
        <div className="bento">
          <div className="tile span7">
            <div className="k">Mini player</div>
            <h3 className="serif">
              Your video, <em>right in the sidebar.</em>
            </h3>
            <p>
              Picture in picture becomes part of the sidebar, right under your
              tabs, and keeps playing while you browse. No window to drag
              around. Calls stack in the same spot.
            </p>
            <div className="stage mpstage">
              <div className="mwin">
                <div className="lights">
                  <i />
                  <i />
                  <i />
                </div>
                <div className="pagecard" />
                <div className="foot">
                  <i />
                  <span>
                    <b />
                    <b />
                    <b />
                  </span>
                  <i />
                </div>
              </div>
              <div className="rows">
                <div className="row task">
                  <Folder color="#188038" />
                  Ship the launch
                </div>
                <div className="row child on">
                  <Favicon domain="figma.com" />
                  Pricing page, final
                </div>
                <div className="row child">
                  <Favicon domain="notion.so" />
                  Launch checklist
                </div>
                <div className="row child">
                  <Favicon domain="youtube.com" />
                  Lisbon 4K walk
                  <svg
                    className="playing"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    aria-hidden="true"
                  >
                    <rect x="2" y="4" width="20" height="16" rx="2" />
                    <rect
                      x="12"
                      y="11"
                      width="7"
                      height="6"
                      rx="1"
                      fill="currentColor"
                    />
                  </svg>
                </div>
              </div>
              <div className="video">
                <Image
                  src={tram}
                  alt="A yellow Lisbon tram, playing docked in the sidebar"
                  sizes="262px"
                />
                <div className="ctl">
                  <svg
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="1.75"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    aria-hidden="true"
                  >
                    <path d="M3 12a9 9 0 1 0 3-6.7L3 8" />
                    <path d="M3 3v5h5" />
                  </svg>
                  <b>
                    <svg viewBox="0 0 24 24" fill="#fff" aria-hidden="true">
                      <rect x="6" y="5" width="4" height="14" rx="1" />
                      <rect x="14" y="5" width="4" height="14" rx="1" />
                    </svg>
                  </b>
                  <svg
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="1.75"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    aria-hidden="true"
                  >
                    <path d="M21 12a9 9 0 1 1-3-6.7L21 8" />
                    <path d="M21 3v5h-5" />
                  </svg>
                </div>
                <span className="x">
                  <svg
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2.4"
                    strokeLinecap="round"
                    aria-hidden="true"
                  >
                    <path d="M18 6 6 18M6 6l12 12" />
                  </svg>
                </span>
                <span className="time">12:41 / 38:02</span>
                <div className="bar">
                  <i />
                </div>
              </div>
              <div className="callout">
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
                Docked in the sidebar, under your tabs
              </div>
            </div>
          </div>

          <div className="tile span5">
            <div className="k">Command bar</div>
            <h3 className="serif">
              Type anything. <em>Get anywhere.</em>
            </h3>
            <p>
              Pages, tabs, math, currencies and time zones in one bar.
              Cmd-Return hands it to your agent.
            </p>
            <div className="stage cbstage">
              <div className="cb">
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
                  9am Lisbon in San Francisco
                  <i />
                </div>
                <div className="answer">
                  <div className="from">
                    9:00 AM{" "}
                    <span style={{ color: "var(--muted)" }}>Lisbon</span>
                  </div>
                  <b>
                    <svg
                      viewBox="0 0 24 24"
                      fill="none"
                      stroke="currentColor"
                      strokeWidth="2"
                      strokeLinecap="round"
                      strokeLinejoin="round"
                      aria-hidden="true"
                    >
                      <path d="M5 12h14m-6-6 6 6-6 6" />
                    </svg>
                  </b>
                  <div className="to">
                    1:00 AM<small>San Francisco, same day</small>
                  </div>
                </div>
                <div className="hints">
                  <span>
                    <kbd>↵</kbd>Copy
                  </span>
                  <span>
                    <kbd>Cmd ↵</kbd>Ask uw
                  </span>
                </div>
              </div>
            </div>
          </div>

          <div className="tile span5">
            <div className="k">Workspaces and themes</div>
            <h3 className="serif">
              A space for every <em>part of your life.</em>
            </h3>
            <p>
              Swipe between Work and Home. Each keeps its own tabs and its own
              colors, with glass and grain if you like.
            </p>
            <div className="stage wsstage">
              <div
                className="mini"
                style={{
                  left: 28,
                  transform: "rotate(-6deg)",
                  background: "linear-gradient(160deg,#CFEFDD,#D1E9EE)",
                }}
              >
                <div className="r t">
                  <Folder color="#188038" />
                  Weekend in Sintra
                </div>
                <div className="r c on">
                  <Favicon domain="booking.com" size={13} />
                  Quinta da Regaleira
                </div>
                <div className="r c">
                  <Favicon domain="maps.google.com" size={13} />
                  Lisbon to Sintra
                </div>
                <div className="ws">
                  <i>💼</i>
                  <i className="c">🏡</i>
                  <i>🎨</i>
                </div>
              </div>
              <div
                className="mini"
                style={{
                  left: 194,
                  top: 22,
                  zIndex: 2,
                  color: "#fff",
                  backgroundColor: "#2B2542",
                  backgroundImage:
                    "radial-gradient(rgba(255,255,255,.07) 1px,transparent 1px),linear-gradient(160deg,#2B2542,#253242)",
                  backgroundSize: "3px 3px,auto",
                }}
              >
                <div className="r t">
                  <Folder color="#C58AF9" />
                  Album artwork
                </div>
                <div
                  className="r c"
                  style={{ background: "rgba(255,255,255,.16)" }}
                >
                  <Favicon domain="figma.com" size={13} />
                  Cover, round 3
                </div>
                <div className="r c">
                  <Favicon domain="spotify.com" size={13} />
                  Reference playlist
                </div>
                <div className="ws">
                  <i>💼</i>
                  <i>🏡</i>
                  <i
                    className="c"
                    style={{ background: "rgba(255,255,255,.16)" }}
                  >
                    🎨
                  </i>
                </div>
              </div>
              <div
                className="mini"
                style={{
                  left: 356,
                  transform: "rotate(6deg)",
                  background: "linear-gradient(160deg,#E7E1F6,#E1E9F6)",
                }}
              >
                <div className="r t">
                  <Folder color="#1A73E8" />
                  Q4 launch review
                </div>
                <div className="r c on">
                  <Favicon domain="notion.so" size={13} />
                  Launch checklist
                </div>
                <div className="r c">
                  <Favicon domain="linear.app" size={13} />
                  Launch blockers
                </div>
                <div className="ws">
                  <i className="c">💼</i>
                  <i>🏡</i>
                  <i>🎨</i>
                </div>
              </div>
            </div>
          </div>

          <div className="tile span7">
            <div className="k">Agent</div>
            <h3 className="serif">
              An agent that <em>asks first.</em>
            </h3>
            <p>
              Pick any model, from Claude to a local one on your Mac. Connect
              your tools. It reads, compares and tidies, then waits for your
              yes.
            </p>
            <div className="stage agstage">
              <div className="chat">
                <div className="me">
                  Which Linear issues block the launch? Close the tabs I&apos;m
                  done with.
                </div>
                <div className="bot">
                  Three issues block it, two assigned to you. I found{" "}
                  <b>5 tabs</b> you haven&apos;t touched since you merged.
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
              <div className="conn">
                <small>Works with your tools</small>
                <div className="grid">
                  <span>
                    <Favicon domain="linear.app" size={18} />
                    Linear
                  </span>
                  <span>
                    <Favicon domain="sentry.io" size={18} />
                    Sentry
                  </span>
                  <span>
                    <Favicon domain="vercel.com" size={18} />
                    Vercel
                  </span>
                  <span>
                    <Favicon domain="notion.so" size={18} />
                    Notion
                  </span>
                </div>
                <div className="chips">
                  <span>Memory</span>
                  <span>Skills</span>
                  <span>Scheduled chats</span>
                </div>
              </div>
            </div>
          </div>

          <div className="tile span6 small">
            <div className="k">Watch this page</div>
            <h3 className="serif">
              It keeps <em>an eye out.</em>
            </h3>
            <p>
              Watch a price, a page, or anything that changes. Or schedule a
              chat for every morning. uw pings you when it moves.
            </p>
            <div className="watch">
              <div className="ping">
                <Image src="/logo.svg" alt="" width={18} height={18} />
                Flights to Lisbon dropped to $548
              </div>
              <div className="h">
                <svg
                  viewBox="0 0 24 24"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="2"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  aria-hidden="true"
                >
                  <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z" />
                  <circle cx="12" cy="12" r="3" />
                </svg>
                SFO to Lisbon, May 14
              </div>
              <div className="v">
                <b>$548</b>
                <span>−$84 this week</span>
              </div>
              <svg
                className="chart"
                viewBox="0 0 300 70"
                preserveAspectRatio="none"
                aria-hidden="true"
              >
                <defs>
                  <linearGradient id="watch-fill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0" stopColor="#6E56CF" stopOpacity=".22" />
                    <stop offset="1" stopColor="#6E56CF" stopOpacity="0" />
                  </linearGradient>
                </defs>
                <path
                  d="M0 18 L40 14 L80 22 L120 16 L160 28 L200 26 L240 44 L270 40 L300 56 L300 70 L0 70Z"
                  fill="url(#watch-fill)"
                />
                <path
                  d="M0 18 L40 14 L80 22 L120 16 L160 28 L200 26 L240 44 L270 40 L300 56"
                  fill="none"
                  stroke="#6E56CF"
                  strokeWidth="2.5"
                  strokeLinejoin="round"
                />
                <circle cx="300" cy="56" r="4" fill="#6E56CF" />
              </svg>
              <div className="note">Checked every morning at 8:00</div>
            </div>
          </div>

          <div className="tile span6 small dark">
            <div className="k">Terminal</div>
            <h3 className="serif">
              Lives in your <em>terminal too.</em>
            </h3>
            <p>
              A real shell in every task. And a uw command that lets Claude Code
              and other agents work in your tabs.
            </p>
            <div className="term">
              <div className="bar lights">
                <i />
                <i />
                <i />
              </div>
              <pre>
                <span className="p">~ $</span> uw chat &quot;what&apos;s left
                before the launch?&quot;
                {"\n"}
                <span className="dim">Reading 6 tabs in Ship the launch</span>
                {"\n"}
                <span className="ok">●</span> 3 blockers in Linear, 1 failing
                check.
                {"\n  "}The pricing page is approved.{"\n"}
                <span className="p">~ $</span> claude mcp add uw -- uw mcp serve
                {"\n"}
                <span className="ok">✓</span> Added uw
              </pre>
            </div>
          </div>
        </div>
      </section>

      <div className="facts">
        <div className="fact">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <circle cx="12" cy="12" r="9" />
            <circle cx="12" cy="12" r="3.5" />
            <path d="M12 8.5h8.5M9 13.7 4.8 6.4M15 13.7 10.8 21" />
          </svg>
          <h4>Feels like home</h4>
          <p>
            Built on Chromium. Your sites, passwords and Chrome extensions just
            work.
          </p>
        </div>
        <div className="fact">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <path d="M21 2l-2 2m-7.6 7.6a5.5 5.5 0 1 1-7.8 7.8 5.5 5.5 0 0 1 7.8-7.8zm0 0L15.5 7.5m0 0 3 3L22 7l-3-3m-3.5 3.5L19 4" />
          </svg>
          <h4>Your AI, your key</h4>
          <p>
            OpenAI, Anthropic, or a model running on your own Mac with Ollama.
          </p>
        </div>
        <div className="fact">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
          </svg>
          <h4>Private by default</h4>
          <p>
            Nothing is reported unless you say yes. Your browsing stays yours.
          </p>
        </div>
        <div className="fact">
          <svg
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <path d="M9 14 4 9l5-5" />
            <path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11" />
          </svg>
          <h4>You&apos;re in charge</h4>
          <p>Every move uw makes can be undone. Keep a tab where you put it.</p>
        </div>
      </div>

      <section className="close">
        <Image src="/logo.svg" alt="uw logo" width={96} height={96} />
        <h2 className="serif">
          Your tabs have been
          <br />
          <em>waiting for this.</em>
        </h2>
        <p>
          uw is in early preview: new, a little rough, and getting better every
          week. Try it and tell us what you think.
        </p>
        <div className="actions">
          <Download />
          <a className="src" href={REPO}>
            Read the source &rarr;
          </a>
        </div>
      </section>

      <footer>
        <a className="brand" href="/">
          <Image
            src="/logo.svg"
            alt=""
            width={30}
            height={30}
            style={{ margin: -2 }}
          />
          uw
        </a>
        <span>© 2026 uw. Built on Chromium.</span>
        <nav>
          <a href={`${REPO}/releases`}>Releases</a>
          <a href={`${REPO}/issues/new`}>Report an issue</a>
          <a href={REPO}>Source</a>
        </nav>
      </footer>
    </main>
  );
}
