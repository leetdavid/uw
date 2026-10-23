// Builds the uw://chat and uw://ai pages and the watch card for Chromium's
// build: checks their types, bundles their scripts, writes their stylesheet,
// and puts them in one folder with the pages' HTML, icons, and fonts. GN
// runs it with Chromium's node from the build directory:
//   bundle.ts --out <dir> --gen <root gen dir> --depfile <file>
import { execFileSync } from "node:child_process";
import { copyFileSync, mkdirSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import * as esbuild from "esbuild";

const app = dirname(fileURLToPath(import.meta.url));
const modules = join(app, "node_modules");
const { values } = parseArgs({
  options: {
    out: { type: "string" },
    gen: { type: "string" },
    depfile: { type: "string" },
  },
});
if (!values.out || !values.gen || !values.depfile) {
  throw new Error("Usage: bundle.ts --out <dir> --gen <dir> --depfile <file>");
}
const out = resolve(values.out);
const gen = resolve(values.gen);
const mojom = join(gen, "uw/browser/ui/webui/uw_chat/uw_chat.mojom-webui.ts");
const providersMojom = join(gen, "uw/browser/ui/webui/uw_providers/uw_providers.mojom-webui.ts");
mkdirSync(out, { recursive: true });

// The app's own settings, plus the generated bindings and the types of
// Chromium's shared //resources scripts that they import.
const tsconfig = join(out, "tsconfig.json");
writeFileSync(
  tsconfig,
  JSON.stringify({
    extends: join(app, "tsconfig.json"),
    compilerOptions: {
      paths: {
        "@/*": [join(app, "src/*")],
        "uw-chat-mojom": [mojom],
        "uw-providers-mojom": [providersMojom],
        "//resources/*": [join(gen, "ui/webui/resources/tsc/*")],
      },
    },
    include: [join(app, "src"), mojom, providersMojom],
  }),
);
execFileSync(process.execPath, [join(modules, "typescript/bin/tsc"), "-p", tsconfig], {
  stdio: "inherit",
});

const bundled = await esbuild.build({
  entryPoints: {
    chat: join(app, "src/main.tsx"),
    providers: join(app, "src/providers/main.tsx"),
    watch_card: join(app, "src/watch-card/main.tsx"),
  },
  outdir: out,
  bundle: true,
  format: "esm",
  minify: true,
  target: "chrome140",
  jsx: "automatic",
  tsconfig: join(app, "tsconfig.json"),
  define: { "process.env.NODE_ENV": '"production"' },
  // Chat messages carry no files, so the PDF preview never loads.
  external: ["pdfjs-dist"],
  plugins: [
    {
      name: "uw-chat",
      setup(build) {
        build.onResolve({ filter: /^uw-chat-mojom$/ }, () => ({ path: mojom }));
        build.onResolve({ filter: /^uw-providers-mojom$/ }, () => ({ path: providersMojom }));
        build.onResolve({ filter: /^shiki$/ }, () => ({
          path: join(app, "src/code-languages.ts"),
        }));
        // Markdown's browser build decodes entities such as &amp; through
        // innerHTML, which the page's Trusted Types rules refuse. Its plain
        // build looks them up in a table.
        build.onResolve(
          { filter: /^decode-named-character-reference$/ },
          ({ path, resolveDir }) => ({
            path: createRequire(join(resolveDir, "index.js")).resolve(path),
          }),
        );
        // Chromium serves these to every WebUI page.
        build.onResolve({ filter: /^\/\/resources\// }, ({ path }) => ({
          path,
          external: true,
        }));
      },
    },
  ],
  metafile: true,
  logLevel: "warning",
});

const tailwind = join(modules, "@tailwindcss/cli/dist/index.mjs");
execFileSync(
  process.execPath,
  [tailwind, "-i", "src/page.css", "-o", join(out, "chat.css"), "--minify"],
  { cwd: app, stdio: "inherit" },
);

const page = dirname(app);
for (const file of [
  "chat.html",
  "providers.html",
  "watch_card.html",
  "favicon.svg",
  "plug.svg",
]) {
  copyFileSync(join(page, file), join(out, file));
}

// The providers' icons, and Exa's for the connect dialog, from
// @lobehub/icons-static-svg.
mkdirSync(join(out, "icons"), { recursive: true });
for (const icon of ["openai", "anthropic", "openrouter", "vercel", "ollama", "exa"]) {
  copyFileSync(
    join(modules, "@lobehub/icons-static-svg/icons", `${icon}.svg`),
    join(out, "icons", `${icon}.svg`),
  );
}

const fonts = join(modules, "@fontsource-variable/inter/files");
copyFileSync(join(fonts, "inter-latin-opsz-normal.woff2"), join(out, "inter-latin.woff2"));
copyFileSync(
  join(fonts, "inter-latin-ext-opsz-normal.woff2"),
  join(out, "inter-latin-ext.woff2"),
);

// Everything the page was built from, so the build reruns this when any of
// it changes.
const inputs = [
  ...Object.keys(bundled.metafile.inputs).map((input) => resolve(input)),
  ...["page.css", "chat.css", "uw-theme.css"].map((file) => join(app, "src", file)),
  join(app, "bundle.ts"),
  join(app, "package.json"),
];
writeFileSync(
  values.depfile,
  `${relative(process.cwd(), join(out, "chat.js"))}: ${inputs
    .map((input) => input.replaceAll(" ", "\\ "))
    .join(" ")}\n`,
);
