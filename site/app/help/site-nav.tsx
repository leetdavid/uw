import Image from "next/image";
import { download, repo } from "@/lib/shared";

export function SiteNav() {
  return (
    <nav className="nav help-nav">
      <a className="brand" href="/">
        <Image src="/logo.svg" alt="" width={30} height={30} />
        uw
      </a>
      <div className="links">
        <a href="/#how">How it works</a>
        <a href="/#features">Features</a>
        <a className="on" href="/help">
          Help
        </a>
        <a href={repo}>GitHub</a>
        <a
          className="get"
          href={download}
          data-event="download_click"
          data-place="nav"
        >
          Download
        </a>
      </div>
    </nav>
  );
}
