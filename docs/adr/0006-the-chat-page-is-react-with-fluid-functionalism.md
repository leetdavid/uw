# The chat page is React with Fluid Functionalism

The user asked for the `uw://chat` page to use [Fluid Functionalism](https://github.com/mickadesign/fluid-functionalism), a set of React components with motion that explains each state change. On 2026-10-15 we decided to use its real components rather than copy its look by hand. Its parts match what the chat page needs: chat messages, thinking steps, an ask-the-user card for approvals, a sidebar for the chat list, and a composer.

The page is its own pnpm workspace package in `browser/resources/uw_chat/app`. The shadcn CLI copied the components into it, so uw owns that code. `app/bundle.ts` checks the page's types, bundles it with esbuild, builds its stylesheet with Tailwind, and adds the Inter font. Chromium's build runs that script with Chromium's own node, so the bundle never goes stale. The bundle carries the page's mojo bindings and loads Chromium's shared mojo scripts from `chrome://resources`.

This adds a second way of building WebUI beside Chromium's `build_webui`, with React, Motion, Radix, and Tailwind as dependencies, a bundle of about 550 kB, and code copied from a registry that uw has to update by hand. In return, uw does not rebuild components that already exist. React runs under the WebUI's Trusted Types and script rules unchanged. `UwChatBrowserTest.ThePageRunsWithoutConsoleErrors` fails if that stops being true.

Considered options: Fluid's rules applied by hand in plain TypeScript, or in Lit, Chromium's own component library (rejected: either is a hand-made copy of components that already exist). Running the bundler from `pnpm run build` before Chromium's build (rejected: a build started with `autoninja` would serve a stale page).

On 2026-10-15 a second page was agreed for v2.1: `uw://providers` is built from the same package, with its own entry in `app/bundle.ts`, so its model and effort chips are the chat composer's own components. See the [AI providers plan](../plans/ai-providers.md).
