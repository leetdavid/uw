# Native libghostty terminal

Written 2026-10-14. The user wants to explore full libghostty for a native terminal after the [ghostty-web terminal tab](../plans/terminal-tab.md) works. Nothing here was built. The claims about libghostty come from web search results and a third-party mirror of its API overview, not from the upstream Ghostty repository. Check them there first.

Implementability: **3/10**, a guess, using the [roadmap convention](../plans/roadmap.md#implementability-estimates).

## What it would add

Ghostty's own Metal renderer, font handling, and config files. libghostty also runs the shell itself, so uw's shell host would go.

## What it would need

- A tab whose content is a native view. Chromium's tabs hold web pages, so uw would keep a placeholder page and lay the Ghostty view over it. Focus, key routing, tab dragging between windows, hover previews, and split view would each need handling.
- An embedding API uw can rely on. The mirrored overview says the API is used mainly by Ghostty's own macOS app and may change between releases.
- Zig in the build, or a prebuilt library.

## What carries over from the first version

The terminal tab's place in the tab tree, the input rule, the busy terminal warning, restore in the same folder, and the opening controls. The page and the shell host would be replaced.

## When to reopen this

Either of these:

- libghostty publishes an embedding API it calls stable.
- ghostty-web's drawing or typing falls short in the user's daily use.

## First things to check

- The upstream repository's current statement on embedding, and whether any third-party app ships with it.
- Whether a native view can sit over a tab's content area in Chromium's macOS window without breaking tab dragging and previews. A two-day trial would turn the 3/10 into a measured score.
