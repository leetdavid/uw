# Terminal tab

Status: scope agreed on 2026-10-14. The [spike](#spike) ran the same day and passed. Its [findings](../research/terminal-spike.md) are in. The shell host and the page, T33 and T34, were approved and built the same day. T35 to T38 still wait for approval.

Implementability: **6/10** for the first version, using the [roadmap convention](roadmap.md#implementability-estimates). It is uw's first page of its own outside settings. ghostty-web is at 0.4.0, and the spike found [gaps in its JavaScript](../research/terminal-spike.md#gaps-uw-has-to-cover) that uw's page has to cover. The close warning touches four of Chromium's closing paths. The spike removed two risks: WebAssembly runs under the page's policy with one change, and a shell started by `posix_spawn` behaves.

The decision to draw with ghostty-web is recorded in [ADR 0004](../adr/0004-the-terminal-draws-with-ghostty-web.md). The agreed product behavior is in [Product](../product.md#terminal-tabs). The tab's look and the settings section still need a "Terminal" page on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs). Nothing is drawn there yet.

## Scope

- A [terminal tab](../CONTEXT.md#language) runs the user's login shell, started as a login shell in their home folder.
- It opens from "New terminal" on the sidebar's "+" menu and the File menu, from Ctrl+`, and from `uw://terminal` typed in the address bar. It opens where a blank new tab would. Changed on 2026-10-14: the [keyboard and mouse plan](keyboard-and-mouse.md#terminal-key) makes Ctrl+` go to the task's terminal and back, and moves New terminal to Ctrl+Shift+`.
- It sits in tasks and nests like any regular tab, and the organizer files it like one. A request describes it by its title and the shell's folder, never by what the shell printed. Moving to another folder asks again; a new title alone does not, since the shell sets one for every command. Changed on 2026-10-14. Before, the organizer left terminal tabs alone.
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
- `browser/resources/uw_terminal/` and `browser/ui/webui/uw_terminal/` hold the page and its Mojo link. The page's content security policy adds `'wasm-unsafe-eval'` to `script-src` and `data:` to `connect-src`, where ghostty-web reads its WebAssembly. It loosens nothing else.
- ghostty-web is pinned to an exact version in `package.json`, and `pnpm-lock.yaml` holds its integrity hash. The prepare step links the installed package into the checkout at `//uw/ghostty-web`, as it links `browser/` and `components/`. The page build packs `dist/ghostty-web.js` as shipped and takes the page's types from `dist/index.d.ts`. The `.wasm` file is built into that script. An update is a deliberate version bump. uw pins the prerelease `0.4.0-next.20.g1858a59`, because 0.4.0 sends no clicks to a program that asks for the mouse, such as a TUI.
- A key listener on the page, in the capture phase, keeps Cmd combinations, Ctrl+Tab, and Ctrl+` away from ghostty-web without cancelling them. For Option with a key it sends Esc and the key's own character on the user's keyboard layout, which the Keyboard Map API gives. ghostty-web alone types a Cmd shortcut's letter into the shell.
- The page never writes an empty chunk and sets scrollback as a byte budget: 10 MB, Ghostty's own default. For T35 it replaces ghostty-web's `window.open` call for a link with a request to the browser.
- Cmd-C and the Edit menu copy the terminal's selection. Dropped text is pasted. A dropped file is ignored, so it cannot replace the page.
- When the shell exits, its tab closes. A shell that cannot start leaves the tab open with a line saying so.
- The tab's icon is a dark rounded square with a prompt in it.
- Until T38, the terminal takes the page card's colors from the `uw-arc-style` guide on the Doop canvas, light or dark with the system, with the task colors as its palette. These are uw's choices and open to review.
- A Cmd-clicked link asks the browser to open a tab with the terminal tab as its source. The [tab tree](native-tab-tree.md) already nests a tab under its source.
- The shell host starts the shell with `posix_spawn`, as a session leader that opens the terminal itself. Chromium's `base::LaunchProcess` cannot give a shell a controlling terminal on macOS.
- A shell is busy when the foreground process group of its pseudo-terminal is not the shell's own.
- The folder is read from the shell's process and saved with the tab's session data, the same route parent links take.
- Each shell lives on a sequence of its own, behind `ShellSession`. Ending a shell can block for up to two seconds on macOS, and that must not hold up the window or another terminal.
- The page acknowledges each piece of output before the next is read, so a command that prints faster than the page draws is slowed down.
- The shell is the one in the user's account record, started as a login shell. uw sets `LANG` to the browser's language with UTF-8 when it has no locale of its own, as it does when started from the Finder.
- Terminal tabs are marked not auto-discardable, the flag Chromium's discard policy already checks.
- New commands reuse uw's command numbers and add no `IDC_` value.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T32: the spike below. It left `components/terminal/shell_host` and its unit tests.
2. T33: the rest of the shell host: reading output on a task runner, writes that never block, closing off the main thread, and the login shell's environment.
3. T34: the terminal page, its link to the shell host, the opening controls, and the organizer filing it by title and folder.
4. T35: links open as child tabs.
5. T36: the busy terminal warning.
6. T37: restore in the same folder.
7. T38: the settings section.

## Spike

Two checks, about a day. Neither changes the browser's interface or its resources, so neither needs the long rebuild.

1. ghostty-web runs in a local test page served with the content security policy a `uw://` page would have, opened in headless uw. It is fed recorded output from `vim` and `htop`, a color test, and one large burst of output. Pass: the screen matches what Ghostty draws for the same input, the page stays responsive during the burst, and the only policy change needed is allowing WebAssembly.
2. The shell host is written as a C++ class with a unit test. Pass: the test starts a shell, sends a command, reads its output, resizes, sees the new size from inside the shell, and finds no process left after closing.

The [findings](../research/terminal-spike.md) say what passed, what was not checked, and what uw's page has to cover.

## Acceptance checks

1. Ctrl+` opens a terminal tab below the active tab's branch with a prompt in the user's home folder.
2. `vim`, `htop`, and a 24-bit color test draw correctly, and still do after the window is resized.
3. With automatic organization on, a new terminal tab is filed like a page. Its request carries the title and a `Folder:` line, and nothing the shell printed. `AutomaticOrganizerBrowserTest.ATerminalTabIsFiledByItsTitleAndFolder` checks it.
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

- Where the tab's title comes from. ghostty-web reports a title the shell sets only when the whole escape sequence arrives in one string. Proposal: the folder and the running command, which the shell host already reports. This would change the title rule in the scope above, so it needs a decision.
- Whether the browser runs a shortcut that the page's key listener leaves alone. The listener leaves Ctrl+` alone and the File menu carries it, but macOS runs a menu shortcut only for the window in front, which a headless test does not have. It needs a check by hand.
- Whether typing feels fast enough by hand. Output speed is: 20 MB drew in under a second.
- What carries the folder when a closed terminal tab is reopened with Cmd-Shift-T. uw's saved tab data does not come back on that path today. It must not be the address.
- What Cmd-R does in a terminal tab. Today it reloads the page, which ends the shell and starts a new one. Proposal: nothing.
- How a new theme reaches an open terminal. ghostty-web 0.4.0 cannot change a terminal's colors once it has opened, so a terminal keeps the light or dark colors it opened with. Check 8 holds for the font and its size, but not yet for the theme.
- Whether a selection should go to the clipboard as soon as it is made. ghostty-web does that, unlike most Mac terminals, and uw leaves it so for now.
- Whether terminal tabs open in Incognito and Guest windows.
