import { RootProvider } from "fumadocs-ui/provider/next";
import "./help.css";

const START_HERE: [string, string][] = [
  ["Install uw", "/help/install-uw"],
  ["Add an AI provider", "/help/add-an-ai-provider"],
  ["Tasks", "/help/tasks"],
  ["Chats", "/help/chats"],
];

export default function HelpLayout({ children }: LayoutProps<"/help">) {
  return <RootProvider search={{ links: START_HERE }}>{children}</RootProvider>;
}
