import type { Metadata } from "next";
import { Instrument_Sans, Instrument_Serif } from "next/font/google";
import "./globals.css";

const sans = Instrument_Sans({ subsets: ["latin"], variable: "--font-sans" });
const serif = Instrument_Serif({
  weight: "400",
  style: ["normal", "italic"],
  subsets: ["latin"],
  variable: "--font-serif",
});

export const metadata: Metadata = {
  metadataBase: new URL("https://uwbrowser.com"),
  title: "uw: ur web, ur way",
  description:
    "The calm browser for people with too many tabs. uw sorts them into tasks on its own, keeps your video playing in the sidebar, and brings an agent that asks before it acts.",
  openGraph: {
    title: "uw: ur web, ur way",
    description:
      "The calm browser for people with too many tabs. Early preview for Mac.",
  },
};

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html
      lang="en"
      className={`${sans.variable} ${serif.variable}`}
      suppressHydrationWarning
    >
      <body>{children}</body>
    </html>
  );
}
