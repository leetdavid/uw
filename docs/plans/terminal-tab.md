# Terminal tab

Status: scope agreed on 2026-10-14. Only the [spike](#spike) is approved to build. The other stages wait for approval once the spike's findings are in.

Implementability: **6/10** for the first version, using the [roadmap convention](roadmap.md#implementability-estimates). It is uw's first page of its own outside settings. No Chromium page of this kind at the pinned revision was found to run WebAssembly. ghostty-web is at 0.4.0. The close warning touches four of Chromium's closing paths.

The decision to draw with ghostty-web is recorded in [ADR 0004](../adr/0004-the-terminal-draws-with-ghostty-web.md). The agreed product behavior is in [Product](../product.md#terminal-tabs). The tab's look and the settings section still need a "Terminal" page on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs). Nothing is drawn there yet.

## Scope

- A [terminal tab](../CONTEXT.md#language) runs the user's login shell, started as a login shell in their home folder.
- It opens from "New terminal" on the sidebar's "+" menu and the File menu, from Ctrl+`, and from `uw://terminal` typed in the address bar. It opens where a blank new tab would.
- It sits in tasks and nests like any regular tab. The organizer never moves it, never asks a model about it, and never lists it among a task's tabs in a request.
- Text reaches the shell only from the user's keyboard, paste, or drop. The address never carries a command.
- Cmd shortcuts keep their browser meaning, with Cmd-C and Cmd-V as copy and paste. Ctrl and Option combinations go to the shell, and Option acts as Alt. uw keeps Ctrl+Tab and Ctrl+`.
- The tab's title is the one the shell sets, otherwise the folder's name. The terminal resizes with the window.
- Cmd-click on an `http` or `https` address the shell printed opens it as a child tab of the terminal tab.
- Closing a [busy terminal](../CONTEXT.md#language) asks first. This covers closing the tab, closing its branch or task, closing the window, and quitting uw. One dialog names every command that would be killed. An idle shell closes without a question, and closing the tab kills the shell.
- After a restart, a terminal tab comes back in its last folder with a new shell. uw restores the folder only and never reruns a command. The shell starts the first time the tab is shown.
- A "Terminal" section on uw's settings page sets the font, its size, and a theme from a short built-in list. The default theme follows light and dark mode.
- Memory saver never discards a terminal tab.

## Non-goals

- uw's models reading or typing in a terminal.
- Commands from any source but the user. That is [stage two](#later-stages).
- Find in terminal, split panes, and scrollback saved across a restart.
- A switch for the Option key, and reading the user's Ghostty config.
- A key lock that sends Ctrl+Tab and Ctrl+` to the shell. Revisit it if a program the user runs needs a key uw keeps.

## Design

- `components/terminal/` holds the shell host. It opens a pseudo-terminal, starts the shell, reads and writes, resizes, reports the folder and the running command, and kills the shell. It has no `//chrome/browser` dependency and its own unit tests. `chromeos/process_proxy` is the model. It cannot be used directly because `//chromeos` asserts `is_chromeos`.
- The shell host runs in the browser process. Every byte to the shell passes through one function there. Stage two adds its approval step at that function, with no change to the page.
- `browser/resources/uw_terminal/` and `browser/ui/webui/uw_terminal/` hold the page and its Mojo link. The page's content security policy allows WebAssembly and loosens nothing else.
- ghostty-web is pinned to an exact version in `package.json`, and `pnpm-lock.yaml` holds its integrity hash. The prepare step copies its script and `.wasm` file into the page's resources. An update is a deliberate version bump.
- A Cmd-clicked link asks the browser to open a tab with the terminal tab as its source. The [tab tree](native-tab-tree.md) already nests a tab under its source.
- A shell is busy when the foreground process group of its pseudo-terminal is not the shell's own.
- The folder is read from the shell's process and saved with the tab's session data, the same route parent links take.
- Terminal tabs are marked not auto-discardable, the flag Chromium's discard policy already checks.
- New commands reuse uw's command numbers and add no `IDC_` value.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T32: the spike below.
2. T33: shell host with unit tests.
3. T34: the terminal page, its link to the shell host, the opening controls, and the organizer leaving it alone.
4. T35: links open as child tabs.
5. T36: the busy terminal warning.
6. T37: restore in the same folder.
7. T38: the settings section.

## Spike

Two checks, about a day. Neither changes the browser's interface or its resources, so neither needs the long rebuild.

1. ghostty-web runs in a local test page served with the content security policy a `uw://` page would have, opened in headless uw. It is fed recorded output from `vim` and `htop`, a color test, and one large burst of output. Pass: the screen matches what Ghostty draws for the same input, the page stays responsive during the burst, and the only policy change needed is allowing WebAssembly.
2. The shell host is written as a C++ class with a unit test. Pass: the test starts a shell, sends a command, reads its output, resizes, sees the new size from inside the shell, and finds no process left after closing.

Findings go in `docs/research/terminal-spike.md`. The spike also answers the first three [open questions](#open-questions).

## Acceptance checks

1. Ctrl+` opens a terminal tab below the active tab's branch with a prompt in the user's home folder.
2. `vim`, `htop`, and a 24-bit color test draw correctly, and still do after the window is resized.
3. With automatic organization on, opening and using a terminal tab sends no request that mentions it.
4. A page, an extension, and a `uw://terminal` address with extra text cannot put a character into the shell.
5. Cmd-click on a printed `http://localhost` address opens it indented under the terminal tab.
6. Each of Cmd-W, "Close branch", closing the window, and Cmd-Q asks before killing a running `sleep 600`, and none asks for an idle prompt.
7. After a restart with session restore, a terminal left in `/tmp` comes back titled `tmp` and starts no shell until selected. Once selected, `pwd` prints `/tmp`.
8. Changing the font, size, or theme on the settings page changes an open terminal.
9. Closing a terminal tab leaves no shell process behind.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Later stages

- **Stage two: commands from other sources.** Agreed direction: other parts of uw, and pages the user allows, may ask to run a command, and each needs the user's approval. The prompt and what uw remembers are open. One proposal, not agreed: Chromium's site permission decides which sites may ask, and a three-button prompt (run once, always run this exact command from this site, don't run) decides each command. This stage needs its own plan. Estimate: 4/10.
- **Native libghostty.** See the [research note](../research/native-libghostty-terminal.md) for what it needs and when to reopen it. Estimate: 3/10.

## Open questions

- How the prepare step feeds ghostty-web's script and `.wasm` file into Chromium's page build.
- Whether ghostty-web 0.4.0 finds links in output and reports a click on one, or uw has to.
- Whether ghostty-web's drawing and typing speed are good enough for daily use.
- What carries the folder when a closed terminal tab is reopened with Cmd-Shift-T. uw's saved tab data does not come back on that path today. It must not be the address.
- What Cmd-R does in a terminal tab. Proposal: nothing.
- Whether terminal tabs open in Incognito and Guest windows.
