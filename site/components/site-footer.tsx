import Image from "next/image";
import { repo } from "@/lib/shared";

export function SiteFooter({ className }: { className?: string }) {
  return (
    <footer className={className}>
      <a className="brand" href="/">
        <Image src="/logo.svg" alt="" width={26} height={26} />
        uw
      </a>
      <span>© 2026 uw. Built on Chromium.</span>
      <nav>
        <a href={`${repo}/releases`}>Releases</a>
        <a href={`${repo}/issues/new`}>Report an issue</a>
        <a href={repo}>Source</a>
        <a href="/help/privacy">Privacy</a>
      </nav>
    </footer>
  );
}
