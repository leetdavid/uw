"use client";

import Image from "next/image";
import {
  type AnimationEvent,
  type ReactNode,
  useEffect,
  useRef,
  useState,
} from "react";
import nata from "@/assets/nata.jpg";
import { Favicon, Folder } from "./icons";
import { Sidebar, type SidebarState } from "./sidebar";

const PILE = [
  "kayak.com",
  "github.com",
  "notion.so",
  "youtube.com",
  "lonelyplanet.com",
  "news.ycombinator.com",
  "figma.com",
  "reddit.com",
  "wikipedia.org",
  "stackoverflow.com",
  "amazon.com",
  "linear.app",
  "maps.google.com",
  "spotify.com",
  "slack.com",
  "medium.com",
  "vercel.com",
  "booking.com",
  "x.com",
  "docs.google.com",
  "airbnb.com",
  "nytimes.com",
  "strava.com",
  "mail.google.com",
];

function at(seconds: number) {
  return { animationDelay: `${seconds}s` };
}

export function Ad({
  sidebar,
  download,
}: {
  sidebar: SidebarState;
  download: string;
}) {
  const scenes: { name: string; seconds: number; body: ReactNode }[] = [
    {
      name: "hook dawn",
      seconds: 3,
      body: (
        <div className="line">
          <span className="rise" style={at(0.2)}>
            You opened a tab
          </span>
          <em className="rise" style={at(0.8)}>
            for that.
          </em>
        </div>
      ),
    },
    {
      name: "pile",
      seconds: 4,
      body: (
        <>
          <div className="browser">
            <div className="strip">
              <span className="lights">
                <i />
                <i />
                <i />
              </span>
              <span className="tabs">
                {PILE.map((site, index) => (
                  <b key={site} style={at(0.2 + Math.floor(index / 3) * 0.4)}>
                    <Favicon domain={site} size={20} />
                    <i />
                  </b>
                ))}
              </span>
            </div>
            <div className="page">
              <i />
              <i />
              <i />
              <i />
            </div>
          </div>
          <div className="line">
            <span>
              <span className="word rise" style={at(0.5)}>
                And that.
              </span>{" "}
              <span className="word rise" style={at(1.3)}>
                And that.
              </span>{" "}
              <em className="word rise" style={at(2.1)}>
                And that.
              </em>
            </span>
          </div>
        </>
      ),
    },
    {
      name: "turn dawn",
      seconds: 3,
      body: (
        <>
          <div className="glass">
            <div className="r t rise" style={at(0.9)}>
              <Folder color="#1A73E8" />
              Plan Lisbon trip
            </div>
            <div className="r c rise" style={at(1.1)}>
              <Favicon domain="lonelyplanet.com" size={18} />
              Four days in Lisbon
            </div>
            <div className="r c rise" style={at(1.3)}>
              <Favicon domain="kayak.com" size={18} />
              SFO to Lisbon, May 14
            </div>
            <div className="r t rise" style={at(1.5)}>
              <Folder color="#188038" />
              Ship the launch
            </div>
            <div className="r c rise" style={at(1.7)}>
              <Favicon domain="notion.so" size={18} />
              Launch checklist
            </div>
          </div>
          <span className="paper p1">
            <Favicon domain="github.com" size={18} />
            Run #4821
          </span>
          <span className="paper p2" style={at(0.15)}>
            <i className="chip" style={{ background: "#0F766E" }} />
            Sunlit flat
          </span>
          <span className="paper p3" style={at(0.3)}>
            <i className="chip" style={{ background: "#111" }} />
            Strada AL
          </span>
          <span className="paper p4" style={at(0.45)}>
            <i className="chip" style={{ background: "#2A1A0E" }} />
            Pastel de nata
          </span>
          <div className="meet">
            <Image
              className="rise"
              style={at(1.2)}
              src="/logo.svg"
              alt=""
              width={120}
              height={120}
            />
            <div className="line">
              <span className="rise" style={at(1.4)}>
                Meet <em>uw.</em>
              </span>
            </div>
          </div>
        </>
      ),
    },
    {
      name: "tasks tint",
      seconds: 4,
      body: (
        <>
          <div className="sb drift">
            <Sidebar state={sidebar} label="" />
          </div>
          <div className="line">
            <span className="rise" style={at(0.3)}>
              It sorts your tabs
            </span>
            <span className="rise" style={at(0.5)}>
              into tasks.
            </span>
            <em className="rise" style={at(1.2)}>
              On its own.
            </em>
          </div>
        </>
      ),
    },
    {
      name: "payoff",
      seconds: 3.5,
      body: (
        <>
          <Image
            className="photo"
            src={nata}
            alt=""
            fill
            sizes="(max-width: 1080px) 100vw, 1040px"
          />
          <div className="caption rise" style={at(0.5)}>
            <div className="line">
              So you can get back to <em>the good part.</em>
            </div>
          </div>
        </>
      ),
    },
    {
      name: "helper tint",
      seconds: 3.5,
      body: (
        <>
          <div className="ask rise" style={at(0.1)}>
            <div className="me">
              Find flats in Alfama under €1,500 for May, and keep the best
              three.
            </div>
            <div className="bot rise" style={at(0.5)}>
              I read 9 listings in <b>Plan Lisbon trip</b>. Three fit.
            </div>
            <div className="wait rise" style={at(0.8)}>
              <small>WAITING FOR YOU</small>
              <b>Close the 6 listings that don&apos;t fit?</b>
              <div className="btns">
                <span className="yes">
                  Close 6 tabs
                  <svg
                    className="pointer"
                    viewBox="0 0 24 24"
                    aria-hidden="true"
                  >
                    <path
                      d="M5 3l14 8-6 2-3 6z"
                      fill="#1E1B2E"
                      stroke="#fff"
                      strokeWidth="1.5"
                      strokeLinejoin="round"
                    />
                  </svg>
                </span>
                <span>Keep them</span>
              </div>
            </div>
          </div>
          <div className="line">
            <span className="rise" style={at(0.4)}>
              Ask for help.
            </span>
            <span className="rise" style={at(0.6)}>
              It <em>asks first.</em>
            </span>
          </div>
        </>
      ),
    },
    {
      name: "calm dawn",
      seconds: 3,
      body: (
        <>
          <div className="line">
            <span className="rise" style={at(0.3)}>
              Calm, <em>on purpose.</em>
            </span>
          </div>
          <div className="window">
            <div className="sb">
              <Sidebar state={{ ...sidebar, notice: undefined }} label="" />
            </div>
            <div className="pane">
              <Image src={nata} alt="" fill sizes="700px" />
            </div>
          </div>
        </>
      ),
    },
    {
      name: "end dawn",
      seconds: 4,
      body: (
        <>
          <Image
            className="rise"
            src="/logo.svg"
            alt=""
            width={140}
            height={140}
          />
          <div className="line">
            <span className="rise" style={at(0.3)}>
              ur web, <em>ur way</em>
            </span>
          </div>
          <div className="cta rise" style={at(0.7)}>
            <a href={download} tabIndex={-1}>
              Download for Mac
            </a>
            uwbrowser.com
          </div>
          <small className="rise" style={at(1)}>
            Early preview · Apple silicon
          </small>
        </>
      ),
    },
  ];

  const [scene, setScene] = useState(0);
  const [previous, setPrevious] = useState<number | null>(null);
  const [paused, setPaused] = useState(false);
  const [visible, setVisible] = useState(true);
  const root = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const observer = new IntersectionObserver(([entry]) =>
      setVisible(entry.isIntersecting),
    );
    if (root.current) observer.observe(root.current);
    return () => observer.disconnect();
  }, []);

  function advance(event: AnimationEvent) {
    if (!(event.target as HTMLElement).classList.contains("clock")) return;
    setPrevious(scene);
    setScene((scene + 1) % scenes.length);
  }

  return (
    <div
      ref={root}
      className={paused || !visible ? "ad paused" : "ad"}
      onAnimationEnd={advance}
    >
      <div aria-hidden="true">
        {scenes.map((item, index) => (
          <div
            key={item.name}
            className={`scene ${item.name}${index === scene ? " on" : index === previous ? " on out" : ""}`}
          >
            {item.body}
            <i
              className="clock"
              style={{ animationDuration: `${item.seconds}s` }}
            />
          </div>
        ))}
      </div>
      <button
        type="button"
        className="pause"
        onClick={() => setPaused(!paused)}
        aria-label={paused ? "Play the ad" : "Pause the ad"}
      >
        <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
          {paused ? (
            <path d="M8 5.5v13a1 1 0 0 0 1.5.86l10.5-6.5a1 1 0 0 0 0-1.72L9.5 4.64A1 1 0 0 0 8 5.5Z" />
          ) : (
            <>
              <rect x="6" y="5" width="4" height="14" rx="1" />
              <rect x="14" y="5" width="4" height="14" rx="1" />
            </>
          )}
        </svg>
      </button>
    </div>
  );
}
