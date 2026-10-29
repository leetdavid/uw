"use client";

import { type ReactNode, useEffect, useRef, useState } from "react";
import poster from "@/assets/film.jpg";

function moves() {
  return (
    CSS.supports("animation-timeline: view()") &&
    matchMedia("(prefers-reduced-motion: no-preference)").matches
  );
}

function Glyph({ children }: { children: ReactNode }) {
  return (
    <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
      {children}
    </svg>
  );
}

const pause = (
  <Glyph>
    <rect x="6" y="4.5" width="4" height="15" rx="1.2" />
    <rect x="14" y="4.5" width="4" height="15" rx="1.2" />
  </Glyph>
);

const play = (
  <Glyph>
    <path d="M7 4.5v15a1 1 0 0 0 1.5.86l12.5-7.5a1 1 0 0 0 0-1.72L8.5 3.64A1 1 0 0 0 7 4.5Z" />
  </Glyph>
);

const replay = (
  <Glyph>
    <path
      d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5"
      fill="none"
      stroke="currentColor"
      strokeWidth="2.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
  </Glyph>
);

const speaker =
  "M11 4.7a.7.7 0 0 0-1.2-.5L6.4 7.6A1.4 1.4 0 0 1 5.4 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.4a1.4 1.4 0 0 1 1 .4l3.4 3.4a.7.7 0 0 0 1.2-.5z";

const muted = (
  <Glyph>
    <path d={speaker} />
    <path
      d="M22 9l-6 6M16 9l6 6"
      fill="none"
      stroke="currentColor"
      strokeWidth="2.2"
      strokeLinecap="round"
    />
  </Glyph>
);

const sound = (
  <Glyph>
    <path d={speaker} />
    <path
      d="M16 9a5 5 0 0 1 0 6M19.4 18.4a9 9 0 0 0 0-12.8"
      fill="none"
      stroke="currentColor"
      strokeWidth="2.2"
      strokeLinecap="round"
    />
  </Glyph>
);

export function Film({
  download,
  children,
}: {
  download: string;
  children: ReactNode;
}) {
  const layer = useRef<HTMLDivElement>(null);
  const video = useRef<HTMLVideoElement>(null);
  const first = useRef<HTMLButtonElement>(null);
  const rest = useRef<HTMLElement>(null);
  const [formed, setFormed] = useState(false);
  const [shown, setShown] = useState(false);
  const [visible, setVisible] = useState(false);
  const [held, setHeld] = useState(false);
  const [ended, setEnded] = useState(false);
  const [playing, setPlaying] = useState(false);
  const [quiet, setQuiet] = useState(true);

  useEffect(() => {
    const film = layer.current;
    if (!film) return;
    const observer = new IntersectionObserver(
      ([entry]) => setVisible(entry.intersectionRatio >= 0.25),
      { threshold: 0.25 },
    );
    observer.observe(film);
    if (!moves()) return () => observer.disconnect();
    let last = scrollY;
    let down = false;
    let timer = 0;
    const settle = () => {
      const opacity = Number(getComputedStyle(film).opacity);
      if (down && opacity > 0 && opacity < 1) {
        rest.current?.scrollIntoView({ behavior: "smooth" });
      }
    };
    const check = () => {
      down = scrollY > last;
      last = scrollY;
      if (video.current && scrollY > 0) video.current.preload = "auto";
      setFormed(getComputedStyle(film).opacity === "1");
      clearTimeout(timer);
      timer = window.setTimeout(settle, 150);
    };
    check();
    addEventListener("scroll", check, { passive: true });
    return () => {
      observer.disconnect();
      removeEventListener("scroll", check);
      clearTimeout(timer);
    };
  }, []);

  useEffect(() => {
    const film = video.current;
    if (!film) return;
    if ((formed || shown) && visible && !held && !ended) {
      film.play().catch(() => {});
    } else {
      film.pause();
    }
  }, [formed, shown, visible, held, ended]);

  function toggle() {
    const film = video.current;
    if (!film) return;
    if (playing) return setHeld(true);
    if (ended) film.currentTime = 0;
    setEnded(false);
    setHeld(false);
    film.play().catch(() => {});
  }

  function show() {
    setShown(true);
    requestAnimationFrame(() => first.current?.focus());
  }

  return (
    <div className={shown ? "track shown" : "track"}>
      <i ref={rest} className="rest" />
      <div className="scene">
        <div className="horizon" />
        <div className="stage">{children}</div>
        <div ref={layer} className="film" inert={!(formed || shown)}>
          <video
            ref={video}
            src="/film.mp4"
            poster={poster.src}
            muted={quiet}
            playsInline
            preload="none"
            aria-label="uw in 40 seconds"
            onPlay={() => setPlaying(true)}
            onPause={() => setPlaying(false)}
            onEnded={() => setEnded(true)}
          />
          {ended && (
            <a className="cta" href={download}>
              <span className="sr-only">Download uw for Mac</span>
            </a>
          )}
          <div className="controls">
            <button
              ref={first}
              type="button"
              aria-label={
                ended
                  ? "Replay the film"
                  : playing
                    ? "Pause the film"
                    : "Play the film"
              }
              onClick={toggle}
            >
              {ended ? replay : playing ? pause : play}
            </button>
            <button
              type="button"
              aria-label={quiet ? "Turn the sound on" : "Turn the sound off"}
              onClick={() => setQuiet(!quiet)}
            >
              {quiet ? muted : sound}
            </button>
          </div>
        </div>
        <button type="button" className="play" onClick={show}>
          <i>{play}</i>
          Play the film
          <small>0:40</small>
        </button>
      </div>
    </div>
  );
}
