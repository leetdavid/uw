import { ArrowRight } from "lucide-react";
import type { Metadata } from "next";
import Image from "next/image";
import { Accent } from "@/components/accent";
import { SearchButton } from "@/components/search-button";
import { repo } from "@/lib/shared";
import { groups } from "@/lib/source";
import { SiteNav } from "./site-nav";

export const metadata: Metadata = {
  title: "uw Help",
  description: "Short pages on everything uw does today.",
};

export default function HelpHome() {
  return (
    <main className="help-home">
      <SiteNav />
      <section className="help-hero">
        <span className="help-group">uw Help</span>
        <h1>
          How to use <em>uw</em>
        </h1>
        <p>
          Short pages on everything uw does today. The agent in uw reads them
          too, so you can just ask it.
        </p>
        <SearchButton />
      </section>
      <div className="help-tiles">
        {groups().map((group) => (
          <div className="help-tile" key={String(group.name)}>
            <span className="kicker">
              {group.pages.length} {group.pages.length === 1 ? "page" : "pages"}
            </span>
            <h2>
              <Accent text={group.name} />
            </h2>
            <ul>
              {group.pages.slice(0, 3).map((page) => (
                <li key={page.url}>
                  <a href={page.url}>
                    {page.name}
                    <ArrowRight />
                  </a>
                </li>
              ))}
            </ul>
          </div>
        ))}
        <div className="help-tile dawn">
          <span className="kicker">For agents</span>
          <h2>
            Help for <em>your agent</em>
          </h2>
          <p>Agents that read the web can fetch every page as Markdown.</p>
          <ul>
            <li>
              <a href="/llms.txt">
                llms.txt
                <ArrowRight />
              </a>
            </li>
            <li>
              <a href="/llms-full.txt">
                llms-full.txt
                <ArrowRight />
              </a>
            </li>
          </ul>
        </div>
      </div>
      <footer className="help-footer">
        <a className="brand" href="/">
          <Image src="/logo.svg" alt="" width={26} height={26} />
          uw
        </a>
        <span>© 2026 uw. Built on Chromium.</span>
        <nav>
          <a href={`${repo}/releases`}>Releases</a>
          <a href={`${repo}/issues/new`}>Report an issue</a>
          <a href={repo}>Source</a>
        </nav>
      </footer>
    </main>
  );
}
