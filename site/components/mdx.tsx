import { Tab, Tabs } from "fumadocs-ui/components/tabs";
import defaultMdxComponents from "fumadocs-ui/mdx";
import type { MDXComponents } from "mdx/types";
import {
  Figure,
  Menu,
  MenuColors,
  MenuItem,
  MenuLine,
  Page,
  Scratchpad,
  Sidebar,
  TabRow,
  Task,
  Window,
} from "./kit";

export function getMDXComponents(components?: MDXComponents) {
  return {
    ...defaultMdxComponents,
    Tab,
    Tabs,
    Figure,
    Window,
    Sidebar,
    Task,
    TabRow,
    Scratchpad,
    Page,
    Menu,
    MenuItem,
    MenuColors,
    MenuLine,
    ...components,
  } satisfies MDXComponents;
}

export const useMDXComponents = getMDXComponents;

declare global {
  type MDXProvidedComponents = ReturnType<typeof getMDXComponents>;
}
