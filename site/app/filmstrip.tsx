"use client";

import { useEffect, useRef, useState } from "react";
import { Sidebar, type SidebarState } from "./sidebar";

export interface Stop {
  title: string;
  text: string;
  sidebar: SidebarState;
}

const STEP = 3500;

export function Filmstrip({ stops }: { stops: Stop[] }) {
  const [lit, setLit] = useState(0);
  const [held, setHeld] = useState(false);
  const [visible, setVisible] = useState(false);
  const list = useRef<HTMLOListElement>(null);

  useEffect(() => {
    const observer = new IntersectionObserver(([entry]) =>
      setVisible(entry.isIntersecting),
    );
    if (list.current) observer.observe(list.current);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (!visible || held) return;
    if (matchMedia("(prefers-reduced-motion: reduce)").matches) return;
    const timer = setTimeout(() => setLit((lit + 1) % stops.length), STEP);
    return () => clearTimeout(timer);
  }, [lit, visible, held, stops.length]);

  return (
    <ol
      ref={list}
      className={held ? "strip held" : "strip"}
      onMouseLeave={() => setHeld(false)}
    >
      {stops.map((stop, index) => (
        <li
          key={stop.title}
          className={index === lit ? "stop on" : "stop"}
          onMouseEnter={() => {
            setLit(index);
            setHeld(true);
          }}
        >
          <div className="rule" />
          <div className="n">0{index + 1}</div>
          <h3>{stop.title}</h3>
          <p>{stop.text}</p>
          <div className="mini">
            <Sidebar state={stop.sidebar} label={stop.title} />
          </div>
        </li>
      ))}
    </ol>
  );
}
