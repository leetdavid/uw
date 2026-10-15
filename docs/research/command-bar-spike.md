# Command bar spike

Run on 2026-10-15 for ticket T60 of the [command bar plan](../plans/command-bar.md#stages). Headless browser tests, two keyboard tests in a real window, and the user's own check by hand.

## Result

| Check | Result |
| --- | --- |
| A panel over the page card opens on Cmd+T without creating a tab | Passed: `CommandBarBrowserTest.CmdTOpensItWithoutATab` |
| Suggestions come from Chromium's searchbox handler | Passed: `TypingListsSuggestions` gets rows back for typed text |
| The settings switch turns it off and Cmd+T opens a blank tab again | Passed: `TurnedOffCmdTOpensATab`, `TurningItOffClosesIt`, and `UwOrganizationPageBrowserTest.TheCommandBarSwitchStartsOnAndTurnsItOff` |
| Escape closes it and opens nothing | Passed in a real window: `MANUAL_EscapeClosesItAndOpensNothing`, and by hand |
| Keys typed right after Cmd+T land in the field in order | Failed for fast keys. Passed by hand at human speed. See [below](#keys-typed-right-after-cmdt). |

## Keys typed right after Cmd+T

`MANUAL_KeysTypedRightAfterCmdTLand` posts Cmd+T and "github" through the app's event queue in a real window. The panel shows and becomes the key window at once, but the field stays empty: every key arrives before the page has focus, and the page drops it.

A temporary build waited a set time between Cmd+T and the first key, three runs each:

| Gap after Cmd+T | Result |
| --- | --- |
| 0, 5, 10, 20 ms | 0 of 3: every key lost |
| 40 ms | 1 of 3 |
| 80 ms | 3 of 3 |

The user typed by hand right after Cmd+T and every letter landed. A fast typist can still press the first key inside 40 ms, so uw has to hold keys in the browser until the field has focus, as the plan's [design](../plans/command-bar.md#design) says. The test stays as the check for that, and should pass 100 of 100 runs once uw holds the keys.

## What the spike found

- **Keeping the page ready.** Chromium's `WebUIContentsPreloadManager` keeps one page loaded for all of Chromium and picks which one. Tab search's `WebUIBubbleManager` drops its page 30 seconds after the bubble closes. Neither keeps the command bar ready, so each window owns a `WebUIContentsWrapper` for as long as the setting is on, and shows it in a `WebUIBubbleDialogView`.
- **Ready to show.** The wrapper counts a page as ready only when the preload manager made it. The command bar shows its bubble at once and does not wait for the page to say it is ready.
- **Suggestions.** `RealboxHandler`, the new tab page's handler, works as it is with no contextual search session. Picking a match would load it inside the panel, because its omnibox client navigates the page that hosts it. T61 needs a handler of uw's own to open pages.
- **WebUI name list.** A top-chrome page's name must be in the `WebUIName` variants of `tools/metrics/histograms/metadata/page/histograms.xml`, or `WebUIContentsWrapperT` fails a compile-time check. `uw-terminal.patch` adds `.UwCommandBar`.
- **Headless keyboard tests.** A headless panel never takes focus, so its page drops keys. A key that nobody handles goes back to the window and into the page again until AppKit aborts in `-[NSView performKeyEquivalent:]`. Escape sent with `content::SimulateKeyPress` never reaches the wrapper's Escape handling. The two keyboard tests are `MANUAL_` tests that post keys through the app's event queue. Run them with `--run-manual --headful-mode`.

## Still open

- Holding keys until the field has focus. Not built.
- The 100 ms target from Cmd+T to the panel on screen. Not measured.
