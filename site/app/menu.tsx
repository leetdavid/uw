"use client";

import { Menu as MenuIcon } from "lucide-react";
import { useRef } from "react";

export function Menu({ links }: { links: [string, string][] }) {
  const menu = useRef<HTMLDivElement>(null);
  return (
    <>
      <button
        type="button"
        className="menu-button"
        popoverTarget="menu"
        aria-label="Menu"
      >
        <MenuIcon size={18} />
      </button>
      <div ref={menu} id="menu" className="menu" popover="auto">
        {links.map(([label, href]) => (
          <a key={href} href={href} onClick={() => menu.current?.hidePopover()}>
            {label}
          </a>
        ))}
      </div>
    </>
  );
}
