import Image from "next/image";
import { repo } from "@/lib/shared";

const DOWNLOAD = `${repo}/releases/latest/download/uw-macos-arm64.dmg`;

export function SiteNav() {
  return (
    <nav className="nav help-nav">
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
        <a href="/#how">How it works</a>
        <a href="/#features">Features</a>
        <a className="on" href="/help">
          Help
        </a>
        <a href={repo}>GitHub</a>
        <a className="get" href={DOWNLOAD}>
          Download
        </a>
      </div>
    </nav>
  );
}
