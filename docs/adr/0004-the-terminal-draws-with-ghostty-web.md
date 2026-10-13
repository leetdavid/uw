# The terminal draws with ghostty-web in a page

The user wants a terminal that behaves like a tab and prefers Ghostty as its engine. On 2026-10-14 we decided that a [terminal tab](../CONTEXT.md#language) is a `uw://terminal` page drawn by [ghostty-web](https://github.com/coder/ghostty-web), which is Ghostty's terminal core compiled to WebAssembly behind an API shaped like xterm.js. uw runs the shell itself in the browser process. A tab's content in Chromium is a web page, so a page gets vertical tabs, tasks, nesting, dragging, and session restore with no new tab code.

uw takes the prebuilt package from npm at an exact version. That trusts Coder's published build with a page that can reach the user's shell, in exchange for keeping Zig and Bun off the build host.

This is not full Ghostty. ghostty-web draws on its own canvas. Ghostty's Metal renderer, its font handling, and its config files are not used.

Considered options: full libghostty in a native view (not now: a tab's content would stop being a page, the embedding API is not described as stable, and the build gains Zig; the [research note](../research/native-libghostty-terminal.md) says when to reopen it); xterm.js (rejected: the user prefers Ghostty's emulation, and the matching API keeps it as a fallback that is mostly an import change); a local helper such as ttyd in an ordinary tab (rejected: a separate process and a listening socket); building the `.wasm` file from Ghostty's source (rejected: uw would own that build).
