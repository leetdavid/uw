import Image from "next/image";
import tram from "@/assets/tram.jpg";
import { Line } from "./icons";

export function Dock() {
  return (
    <div className="dock">
      <Image src={tram} alt="" fill sizes="240px" />
      <span className="controls">
        <Line>
          <path d="M3 12a9 9 0 1 0 3-6.7L3 8" />
          <path d="M3 3v5h5" />
        </Line>
        <b>
          <svg viewBox="0 0 24 24" fill="#fff" aria-hidden="true">
            <rect x="6" y="5" width="4" height="14" rx="1" />
            <rect x="14" y="5" width="4" height="14" rx="1" />
          </svg>
        </b>
        <Line>
          <path d="M21 12a9 9 0 1 1-3-6.7L21 8" />
          <path d="M21 3v5h-5" />
        </Line>
      </span>
      <span className="time">12:41 / 38:02</span>
      <span className="progress">
        <i />
      </span>
    </div>
  );
}
