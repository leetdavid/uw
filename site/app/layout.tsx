import type { Metadata } from "next";
import { Instrument_Serif, Inter } from "next/font/google";
import "./globals.css";

const inter = Inter({ subsets: ["latin"], variable: "--font-sans" });
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
    <html lang="en" className={`${inter.variable} ${serif.variable}`}>
      <body>{children}</body>
    </html>
  );
}
