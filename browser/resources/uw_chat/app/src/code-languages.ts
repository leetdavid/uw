// The languages and themes code blocks in an answer are colored with. The
// Streamdown code plugin imports "shiki", which bundle.ts points here, so the
// page carries these grammars instead of all of Shiki's.
import { createBundledHighlighter } from "shiki/core";
import { createJavaScriptRegexEngine } from "shiki/engine/javascript";

export const bundledLanguagesInfo = [
  {
    id: "shellscript",
    name: "Shell",
    aliases: ["bash", "sh", "shell", "zsh"],
    import: () => import("shiki/langs/shellscript.mjs"),
  },
  { id: "json", name: "JSON", aliases: [], import: () => import("shiki/langs/json.mjs") },
  {
    id: "typescript",
    name: "TypeScript",
    aliases: ["ts"],
    import: () => import("shiki/langs/typescript.mjs"),
  },
  {
    id: "javascript",
    name: "JavaScript",
    aliases: ["js"],
    import: () => import("shiki/langs/javascript.mjs"),
  },
  { id: "python", name: "Python", aliases: ["py"], import: () => import("shiki/langs/python.mjs") },
  { id: "html", name: "HTML", aliases: [], import: () => import("shiki/langs/html.mjs") },
  { id: "css", name: "CSS", aliases: [], import: () => import("shiki/langs/css.mjs") },
  { id: "sql", name: "SQL", aliases: [], import: () => import("shiki/langs/sql.mjs") },
  { id: "yaml", name: "YAML", aliases: ["yml"], import: () => import("shiki/langs/yaml.mjs") },
  { id: "rust", name: "Rust", aliases: ["rs"], import: () => import("shiki/langs/rust.mjs") },
  { id: "go", name: "Go", aliases: [], import: () => import("shiki/langs/go.mjs") },
  { id: "cpp", name: "C++", aliases: ["c++"], import: () => import("shiki/langs/cpp.mjs") },
  { id: "diff", name: "Diff", aliases: [], import: () => import("shiki/langs/diff.mjs") },
];

export const bundledLanguages = Object.fromEntries(
  bundledLanguagesInfo.map((language) => [language.id, language.import]),
);

export const createHighlighter = createBundledHighlighter({
  langs: bundledLanguages,
  themes: {
    "github-light": () => import("shiki/themes/github-light.mjs"),
    "github-dark": () => import("shiki/themes/github-dark.mjs"),
  },
  engine: () => createJavaScriptRegexEngine(),
});
