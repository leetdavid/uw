# Terminal tab

Status: scope agreed on 2026-10-14. The [spike](#spike) ran the same day and passed. Its [findings](../research/terminal-spike.md) are in. The shell host and the page, T33 and T34, were approved and built the same day. T35 to T38 still wait for approval. The [busy terminal warning](#busy-terminal-warning), T36, was agreed and its design signed off on 2026-10-31.

Implementability: **6/10** for the first version, using the [roadmap convention](roadmap.md#implementability-estimates). It is uw's first page of its own outside settings. ghostty-web is at 0.4.0, and the spike found [gaps in its JavaScript](../research/terminal-spike.md#gaps-uw-has-to-cover) that uw's page has to cover. The close warning touches four of Chromium's closing paths. The spike removed two risks: WebAssembly runs under the page's policy with one change, and a shell started on a pseudo-terminal behaves.

The decision to draw with ghostty-web is recorded in [ADR 0004](../adr/0004-the-terminal-draws-with-ghostty-web.md). The agreed product behavior is in [Product](../product.md#terminal-tabs). The tab's look and the settings section still need a "Terminal" page on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs). Nothing is drawn there yet.

## Scope

- A [terminal tab](../CONTEXT.md#language) runs the user's login shell, started as a login shell in their home folder.
- It opens from "New terminal" on the sidebar's "+" menu and the File menu, from Ctrl+\`, and from `uw://terminal` typed in the address bar. It opens where a blank new tab would. Changed on 2026-10-14: the [keyboard and mouse plan](keyboard-and-mouse.md#terminal-key) makes Ctrl+\` go to the task's terminal and back, and moves New terminal to Ctrl+Shift+\`.
- It sits in tasks and nests like any regular tab, and the organizer files it like one. A request describes it by its title and the shell's folder, never by what the shell printed. Moving to another folder asks again; a new title alone does not, since the shell sets one for every command. Changed on 2026-10-14. Before, the organizer left terminal tabs alone.
- Text reaches the shell only from the user's keyboard, paste, or drop, or from the [command bar](command-bar.md#sending-the-text-to-a-chat-or-a-terminal) where the user typed it. In a terminal tab the agent opened, the agent's commands and keys reach it too, after the [approvals](agent-chats.md#agreed-scope) for them. Added on 2026-10-20. The address never carries a command. Changed on 2026-10-15.
- Cmd shortcuts keep their browser meaning, with Cmd-C and Cmd-V as copy and paste. Ctrl and Option combinations go to the shell, and Option acts as Alt. uw keeps Ctrl+Tab and Ctrl+\`. Shift+Enter sends what Option+Enter does, so Claude Code starts a new line instead of sending the prompt. Changed on 2026-10-15. Before, it sent Enter.
- The tab's title is the one the shell sets, otherwise the folder's name. The terminal resizes with the window.
- Cmd-click on an `http` or `https` address the shell printed opens it as a child tab of the terminal tab. A Cmd-click is uw's, as Cmd keys are: a program that asks for the mouse, such as herdr, never sees it. Other clicks go to such a program, and the page opens no link for them. herdr opens a link on Ctrl-click, through `open`, so it lands at the top of the Scratchpad as any [link from another app](../product.md#automatic-tab-organization) does. Changed on 2026-10-19: from 2026-10-16 a Cmd-click went to the program alone, as in Ghostty, but herdr ignores it, so nothing opened. Before that, the page opened the link and also passed the click on, and the user saw two tabs.
- Closing a [busy terminal](../CONTEXT.md#language) asks first. This covers closing the tab, closing its branch or task, closing the window, and quitting uw. One dialog names every command that would be killed. An idle shell closes without a question, and closing the tab kills the shell. See [Busy terminal warning](#busy-terminal-warning).
- After a restart, a terminal tab comes back in its place in the tab tree with a new shell. uw never reruns a command. The shell starts the first time the tab is shown. Its folder, command history, and scrollback come back with T37: see [Later stages](#later-stages). Changed on 2026-10-18: before, only the folder came back, and scrollback was a non-goal.
- A "Terminal" section on uw's settings page sets the font, its size, and a theme from a short built-in list. The default theme follows light and dark mode.
- Memory saver never discards a terminal tab.

## Busy terminal warning

Agreed on 2026-10-31 as T36, to be built with the [close button on a task's row](keyboard-and-mouse.md#close-button-on-a-tasks-row), when the user asked that closing a task ask first while a command runs in it.

- A terminal is busy while any program other than its shell has it. herdr, vim, and htop count, not only builds and servers.
- One dialog asks before any close that would end a busy terminal: closing the tab, Close Other Tabs, Close Tabs Below, Close Branch, Close Task, closing the window, and quitting uw.
- Its title names the close: "Close this tab?", "Close 4 tabs?" for a branch and the other closes of several tabs, "Close this task?", "Close this window?", or "Quit uw?". It lists each busy terminal by its title, with the running program's name as macOS reports it: `pnpm dev` shows as `node`. A window's list names each terminal's task, and quitting's names its workspace.
- Two buttons: one in the close color that names the close, such as Close Task, which Return picks, and Cancel, which Esc picks. There is no "Don't ask again".
- A command that ends while the dialog is open leaves it open. The answer still applies.
- Quitting lists the busy terminals of every window and workspace, hidden ones too.
- With [shared tabs](shared-tabs.md) on, closing a window closes no tabs, so it does not ask.
- Cmd+Shift+T brings a closed terminal back with a new shell. The command does not run again.
- Unchanged: Clear leaves busy terminals open, and the agent's `close` and `close_task` tools and `uw tabs close` refuse them, per the [tab tools plan](tab-tools.md#agreed-scope).
- While it asks about a task, the task's rows stay tinted as under the × of its row.
- The design is the frames "Busy terminal warning: the dialog" and "Close task in one click: every state" on the canvas page "Keyboard and mouse", drawn on 2026-10-31 and signed off by the user the same day.

## Non-goals

- uw's models reading or typing in a terminal the user opened. Changed on 2026-10-20: the agent runs commands in terminal tabs it opens under its chat, per the [agent chats plan](agent-chats.md#agreed-scope). Before, no model read or typed in any terminal.
- Commands from any source but the user. That is [stage two](#later-stages).
- Find in terminal and split panes.
- A switch for the Option key, and reading the user's Ghostty config.
- A key lock that sends Ctrl+Tab and Ctrl+\` to the shell. Revisit it if a program the user runs needs a key uw keeps.

## Design

- `components/terminal/` holds the shell host. It opens a pseudo-terminal, starts the shell, reads and writes, resizes, reports the folder and the running command, and kills the shell. It has no `//chrome/browser` dependency and its own unit tests. `chromeos/process_proxy` is the model. It cannot be used directly because `//chromeos` asserts `is_chromeos`.
- The shell host runs in the browser process. Every byte to the shell passes through one function there. Stage two adds its approval step at that function, with no change to the page.
- `browser/resources/uw_terminal/` and `browser/ui/webui/uw_terminal/` hold the page and its Mojo link. The page's content security policy adds `'wasm-unsafe-eval'` to `script-src` and `data:` to `connect-src`, where ghostty-web reads its WebAssembly. It loosens nothing else.
- ghostty-web is pinned to an exact version in `package.json`, and `pnpm-lock.yaml` holds its integrity hash. The prepare step links the installed package into the checkout at `//uw/ghostty-web`, as it links `browser/` and `components/`. The page build packs `dist/ghostty-web.js` as shipped and takes the page's types from `dist/index.d.ts`. The `.wasm` file is built into that script. An update is a deliberate version bump. uw pins the prerelease `0.4.0-next.20.g1858a59`, because 0.4.0 sends no clicks to a program that asks for the mouse, such as a TUI.
- A key listener on the page, in the capture phase, keeps Cmd combinations, Ctrl+Tab, and Ctrl+\` away from ghostty-web without cancelling them. For Option with a key it sends Esc and the key's own character on the user's keyboard layout, which the Keyboard Map API gives. ghostty-web alone types a Cmd shortcut's letter into the shell.
- ghostty-web reports Cmd to a program as Alt, which Ghostty never does. While a program asks for the mouse, a capture listener keeps Cmd presses and releases from ghostty-web, so only its link opener sees a Cmd-click. A Mac page gets no click event for a Ctrl-click, so ghostty-web never opens a link the program also gets.
- A program that asks for the mouse, such as herdr, tmux, or opencode, gets the wheel as mouse buttons 64 and 65 at the cell under the pointer, one for each line scrolled. ghostty-web alone sends arrow keys on the alternate screen, so such a program cannot scroll. Upstream has an open fix, coder/ghostty-web#136. Added on 2026-10-16 after the user found that opencode in herdr did not scroll.
- The page never writes an empty chunk and sets scrollback as a byte budget: 10 MB, Ghostty's own default. For T35 it replaces ghostty-web's `window.open` call for a link with a request to the browser.
- Cmd-C and the Edit menu copy the terminal's selection. Dropped text is pasted.
- A pasted or dropped file types its path, with a backslash before each character the shell treats specially, as Ghostty does. Several files are separated by spaces. Changed on 2026-10-15 at the user's request. Before, a dropped file was ignored.
- A pasted image with no text and no file is saved as `image.png` in a new folder in the user's temporary folder, and its path is typed. Command-line tools such as Claude Code take an image that way. When the clipboard holds text too, the text is pasted. Added on 2026-10-15 at the user's request.
- A page cannot see file paths, so the browser supplies them. For a paste, the page asks the browser, which reads the clipboard. For a drop, Chromium's drop handler turns the files into text before the page gets them, and the page pastes that text like any dropped text.
- When the shell exits, its tab closes. A shell that cannot start leaves the tab open with a line saying so.
- The tab's icon is a dark rounded square with a prompt in it.
- Until T38, the terminal takes the page card's colors from the `uw-arc-style` guide on the Doop canvas, light or dark with the system, with the task colors as its palette. These are uw's choices and open to review.
- A Cmd-clicked link asks the browser to open a tab with the terminal tab as its source. The [tab tree](native-tab-tree.md) already nests a tab under its source.
- The shell host starts the shell as `forkpty` does: the child starts a new session and takes the terminal as its controlling terminal, then runs the shell. Job control, Ctrl-C, and `sudo`'s password prompt depend on that. Until 2026-10-21 it used `posix_spawn`, which opens files before it starts the session, so zsh, which opens its terminal with `O_NOCTTY`, ran without a controlling terminal. Chromium's `base::LaunchProcess` cannot give a shell one on macOS.
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
6. T37: restore in the same folder, with command history and scrollback.
7. T38: the settings section.

## Spike

Two checks, about a day. Neither changes the browser's interface or its resources, so neither needs the long rebuild.

1. ghostty-web runs in a local test page served with the content security policy a `uw://` page would have, opened in headless uw. It is fed recorded output from `vim` and `htop`, a color test, and one large burst of output. Pass: the screen matches what Ghostty draws for the same input, the page stays responsive during the burst, and the only policy change needed is allowing WebAssembly.
2. The shell host is written as a C++ class with a unit test. Pass: the test starts a shell, sends a command, reads its output, resizes, sees the new size from inside the shell, and finds no process left after closing.

The [findings](../research/terminal-spike.md) say what passed, what was not checked, and what uw's page has to cover.

## Acceptance checks

1. Ctrl+\` opens a terminal tab below the active tab's branch with a prompt in the user's home folder.
2. `vim`, `htop`, and a 24-bit color test draw correctly, and still do after the window is resized.
3. With automatic organization on, a new terminal tab is filed like a page. Its request carries the title and a `Folder:` line, and nothing the shell printed. `AutomaticOrganizerBrowserTest.ATerminalTabIsFiledByItsTitleAndFolder` checks it.
4. A page, an extension, and a `uw://terminal` address with extra text cannot put a character into the shell.
5. Cmd-click on a printed `http://localhost` address opens it indented under the terminal tab.
6. Each of Cmd-W, "Close branch", closing the window, and Cmd-Q asks before killing a running `sleep 600`, and none asks for an idle prompt.
7. After a restart, a terminal left in `/tmp` comes back titled `tmp` and starts no shell until selected. Once selected, `pwd` prints `/tmp`, the lines printed before the restart are above the prompt, and up-arrow brings back the last command.
8. Changing the font, size, or theme on the settings page changes an open terminal.
9. Closing a terminal tab leaves no shell process behind.

Run `pnpm run check`, then the focused `unit_tests` and `browser_tests` suites with `pnpm run verify`. Browser tests stay headless.

## Later stages

- **Restore, T37.** Agreed on 2026-10-18, deferred at the user's request. A restored terminal tab comes back in its last folder, with its command history and its scrollback. On quit, uw lets each shell exit on its own so the shell writes its history file. The page saves its scrollback during the session, so a crash keeps it, and replays it before the new shell starts. Saved scrollback can hold secrets and sits in the profile. Estimate: 5/10.
- **Stage two: commands from other sources.** Agreed direction: other parts of uw, and pages the user allows, may ask to run a command, and each needs the user's approval. The agent's commands, agreed on 2026-10-20, follow their own approval rules in the [agent chats plan](agent-chats.md#agreed-scope) and run only in its own tabs. The prompt and what uw remembers are open. One proposal, not agreed: Chromium's site permission decides which sites may ask, and a three-button prompt (run once, always run this exact command from this site, don't run) decides each command. This stage needs its own plan. Estimate: 4/10.
- **Native libghostty.** See the [research note](../research/native-libghostty-terminal.md) for what it needs and when to reopen it. Estimate: 3/10.

## Open questions

- Where the tab's title comes from. ghostty-web reports a title the shell sets only when the whole escape sequence arrives in one string. Proposal: the folder and the running command, which the shell host already reports. This would change the title rule in the scope above, so it needs a decision.
- Whether the browser runs a shortcut that the page's key listener leaves alone. The listener leaves Ctrl+\` alone and the File menu carries it, but macOS runs a menu shortcut only for the window in front, which a headless test does not have. It needs a check by hand.
- Whether typing feels fast enough by hand. Output speed is: 20 MB drew in under a second.
- What carries the folder when a closed terminal tab is reopened with Cmd-Shift-T. uw's saved tab data does not come back on that path today. It must not be the address.
- What Cmd-R does in a terminal tab. Today it reloads the page, which ends the shell and starts a new one. Proposal: nothing.
- How a new theme reaches an open terminal. ghostty-web 0.4.0 cannot change a terminal's colors once it has opened, so a terminal keeps the light or dark colors it opened with. Check 8 holds for the font and its size, but not yet for the theme.
- Whether a selection should go to the clipboard as soon as it is made. ghostty-web does that, unlike most Mac terminals, and uw leaves it so for now.
- Whether terminal tabs open in Incognito and Guest windows.
