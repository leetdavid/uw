# Terminal spike

Run on 2026-10-14 for ticket T32 of the [terminal tab plan](../plans/terminal-tab.md#spike). The Mac was under heavy load from other builds the whole time, with a load average between 80 and 260.

## Result

ghostty-web 0.4.0 is good enough to build on, and a shell started the way a browser would have to start it behaves. The estimate for the first version stays at 6/10. Ghostty's core draws and parses well. The JavaScript around it has gaps that uw's page has to cover, listed [below](#gaps-uw-has-to-cover).

| Check | Result |
| --- | --- |
| 1. ghostty-web under the policy of a `uw://` page | Passed, with gaps |
| 2. Shell host: the system calls | Passed in a standalone probe, 29 of 30 runs |
| 2. Shell host: the class and its unit tests | Passed: 8 of 8 `ShellHostTest` cases in `unit_tests`, and 200 of 200 when repeated 25 times |

## Check 1: ghostty-web in a page

The test page was served from `127.0.0.1` with the default content security policy of a trusted `chrome://` page, taken from `content/public/browser/url_data_source.cc`. Headless uw opened it, so no window appeared. It was not a real `uw://` page.

| Question | Finding |
| --- | --- |
| Policy | With the default policy the page fails with a `script-src` violation on `wasm-eval`. Adding `'wasm-unsafe-eval'` to `script-src` was the only change the test page needed. Trusted Types stayed enforced with no violation. The real `uw://terminal` page also needed `data:` in `connect-src`: ghostty-web reads its WebAssembly with `fetch()` from a `data:` address, and a Chromium page limits `connect-src`, which the test page's policy left out. |
| Files | The `.wasm` file is built into `ghostty-web.js` as base64. The page ran with that one file present and nothing else from the package. |
| `vim` | Recorded output at 100 by 30 drew correctly with syntax colors. 29 of 30 rows matched tmux's reading of the same bytes. The other row was noise in the reference. |
| `htop` | 30 of 30 rows matched tmux's reading. Meters and the header bar drew correctly. |
| Colors and text | 16 and 256 colors, 24-bit color, bold, dim, italic, underline, inverse, strike, box drawing, CJK wide characters, emoji, and combining marks drew correctly. A curly underline drew as a straight one. |
| Large output | 20 MB of colored lines in 64 KB chunks took 0.93 s, about 21 MB a second. No frame took longer than 17 ms. The largest single write took 7.5 ms. |
| Typing | 300 one-character writes 15 ms apart: the slowest write took 0.5 ms and no frame was late. |
| Links | Built in. Plain click does nothing. Cmd-click calls `window.open(url, '_blank', 'noopener,noreferrer')`, and the pointer changes over a link. |
| Resize | `resize` and the bundled fit helper work and report the new size. |
| Paste | Bracketed paste is honored. Without it, pasted newlines are sent as typed, not turned into returns. |

The text comparison used tmux as the reference, and I read the screenshots myself. I did not compare against the Ghostty app, because that would have opened a window on the desktop.

### Gaps uw has to cover

All of these are in ghostty-web's JavaScript, not in Ghostty's core.

| Gap | What uw's page does about it |
| --- | --- |
| A Cmd shortcut types its letter into the shell. Cmd-T sent `t`. | A key listener in the capture phase stops Cmd combinations before ghostty-web sees them and does not cancel them. Tried in the page: nothing reached the shell and the event was left for the browser. |
| Ctrl+Tab and Ctrl+` are encoded and sent to the shell. | The same listener stops them. |
| Option-B sends `b`, not Esc `b`. | The same listener sends Esc and the letter. Tried: Option-B and Option-F sent the right bytes. |
| `write('')` throws a `RangeError`. | Never pass an empty chunk. |
| A title change fires only when the whole escape sequence arrives in one string. Bytes never fire it, and neither does a sequence split across two writes. | Do not rely on it. Take the title from the folder and the running command, which the shell host already reports. |
| `scrollback: 10000` kept about 500 to 650 lines. The value seems to reach Ghostty as bytes. | Pass a byte budget. |
| `onRender` never fires. | Do not use it. |
| The buffer's text API drops empty cells and combining marks. | Read cells, not strings, if uw ever needs screen text. |

### Not checked

- A real `uw://` page with Chromium's page build and Mojo.
- Whether the browser runs a shortcut that the page's listener leaves alone. A synthetic key press carries no macOS event, so this needs a real key press in a browser test.
- Time from a real key press to pixels. `onRender` would have been the hook for it.
- Cmd-C and Cmd-V with the real clipboard.

## Check 2: shell host

### The system calls

A standalone C++ program started `/bin/sh` on a pseudo-terminal from a process with six other threads. It passed 29 of 30 runs. The one failure came in a batch at a load average near 150, and I did not capture which check it was. The probe used fixed waits, which the unit tests do not.

Each run checked that:

- the shell runs a command and its output comes back
- the terminal is the shell's controlling terminal
- the shell starts in the folder and at the size asked for, with UTF-8 input on
- a resize reaches the shell
- an idle shell is the terminal's foreground process, and a running `sleep` is seen and named
- the shell's folder can be read from outside
- Ctrl-C interrupts the running command, with exit status 130
- closing the terminal ends the shell and a command it was still running

What the probe settled:

- **Start the shell with `posix_spawn`.** The flags are `POSIX_SPAWN_SETSID` and `POSIX_SPAWN_CLOEXEC_DEFAULT`, and the shell opens the terminal itself through a file action. Opening it after it becomes a session leader is what makes the terminal its controlling terminal. Chromium's `base::LaunchProcess` cannot do this on macOS: its `ctrl_terminal_fd` option exists only on ChromeOS.
- **Set the size through the shell's end, and keep that end open until the shell has started.** A size set on the other end before the shell opens the terminal was lost. So was a size set through the shell's end when that descriptor was closed before the shell started. The unit tests caught the second case.
- **Busy is one call.** `tcgetpgrp` on the terminal gives the foreground process group. It equals the shell's process id at a prompt.
- **The folder is one call.** `proc_pidinfo` with `PROC_PIDVNODEPATHINFO` reads it. No shell integration is needed.

### The class

`components/terminal/shell_host` starts a shell, writes to it, resizes it, names the running command, reads the shell's folder, and ends the shell when destroyed. Reading its output on a task runner is left for T33.

Eight `ShellHostTest` cases cover the same list as the probe, against `/bin/sh`. All eight pass in `unit_tests`, and all 200 runs passed when repeated 25 times at four jobs. `pnpm run verify` runs them with the other focused unit tests.

## What is left

- **Not checked:** which app macOS names in a privacy prompt raised by a command in the shell.

## Answers to the plan's open questions

| Question | Answer |
| --- | --- |
| How the prepare step feeds ghostty-web into the page build | Half answered. Only `ghostty-web.js` is needed, 682 kB. How it gets from `node_modules` into Chromium's page build was not tried. |
| Whether ghostty-web finds links | Yes, and it reports a Cmd-click. uw replaces its `window.open` call with a request to the browser. |
| Whether drawing and typing are fast enough | Output speed is. Typing felt by hand is still unknown. |
