import { DocsLayout } from "fumadocs-ui/layouts/docs";
import Image from "next/image";
import { source } from "@/lib/source";
import { SiteNav } from "../site-nav";

export default function Layout({ children }: LayoutProps<"/help">) {
  return (
    <>
      <div className="help-sky" />
      <SiteNav />
      <DocsLayout
        tree={source.getPageTree()}
        nav={{
          title: (
            <>
              <Image src="/logo.svg" alt="" width={26} height={26} />
              uw <span className="help-nav-section">Help</span>
            </>
          ),
          url: "/help",
        }}
        sidebar={{ collapsible: false }}
        themeSwitch={{ mode: "light-dark-system" }}
      >
        {children}
      </DocsLayout>
    </>
  );
}
