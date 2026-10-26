"use client";

import Image, { type StaticImageData } from "next/image";
import { type ReactNode, useEffect, useRef, useState } from "react";

export interface Stop {
  title: ReactNode;
  label: string;
  text: string;
  image: StaticImageData;
  alt: string;
}

export function ScrollDemo({ stops }: { stops: Stop[] }) {
  const [active, setActive] = useState(0);
  const markers = useRef<(HTMLDivElement | null)[]>([]);

  useEffect(() => {
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) {
            setActive(Number((entry.target as HTMLElement).dataset.index));
          }
        }
      },
      { rootMargin: "-50% 0px -50% 0px" },
    );
    for (const marker of markers.current) {
      if (marker) observer.observe(marker);
    }
    return () => observer.disconnect();
  }, []);

  return (
    <>
      <div className="demo">
        <div className="markers" aria-hidden="true">
          {stops.map((stop, index) => (
            <div
              key={stop.label}
              className="marker"
              data-index={index}
              ref={(element) => {
                markers.current[index] = element;
              }}
            />
          ))}
        </div>
        <div className="pin">
          <div className="cap">
            <div className="steps">
              {stops.map((stop, index) => (
                <div
                  key={stop.label}
                  className={index === active ? "step on" : "step"}
                >
                  <div className="num">
                    {index + 1} OF {stops.length}
                  </div>
                  <h3 className="serif">{stop.title}</h3>
                  <p>{stop.text}</p>
                </div>
              ))}
            </div>
            <ol className="idx">
              {stops.map((stop, index) => (
                <li
                  key={stop.label}
                  className={index === active ? "on" : undefined}
                >
                  <span>0{index + 1}</span>
                  {stop.label}
                </li>
              ))}
            </ol>
          </div>
          <div className="sb">
            {stops.map((stop, index) => (
              <Image
                key={stop.label}
                src={stop.image}
                alt={stop.alt}
                className={index === active ? "on" : undefined}
                sizes="340px"
              />
            ))}
          </div>
        </div>
      </div>
      <div className="still">
        {stops.map((stop, index) => (
          <div key={stop.label} className="card">
            <div>
              <div className="kicker">
                {index + 1} of {stops.length}
              </div>
              <h3 className="serif">{stop.title}</h3>
              <p>{stop.text}</p>
            </div>
            <Image src={stop.image} alt={stop.alt} sizes="220px" />
          </div>
        ))}
      </div>
    </>
  );
}
