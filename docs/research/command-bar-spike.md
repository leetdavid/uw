# Command bar spike

Run on 2026-10-15 for ticket T60 of the [command bar plan](../plans/command-bar.md#stages). Headless browser tests, two keyboard tests in a real window, and the user's own check by hand.

## Result

| Check | Result |
| --- | --- |
| A panel over the page card opens on Cmd+T without creating a tab | Passed: `CommandBarBrowserTest.CmdTOpensItWithoutATab` |
| Suggestions come from Chromium's searchbox handler | Passed: `TypingListsSuggestions` gets rows back for typed text |
| The settings switch turns it off and Cmd+T opens a blank tab again | Passed: `TurnedOffCmdTOpensATab`, `TurningItOffClosesIt`, and `UwOrganizationPageBrowserTest.TheCommandBarSwitchStartsOnAndTurnsItOff` |
| Escape closes it and opens nothing | Passed in a real window: `MANUAL_EscapeClosesItAndOpensNothing`, and by hand |
| Keys typed right after Cmd+T land in the field in order | Failed for fast keys. Passed by hand at human speed. Holding keys passed 97 of 100 real-window runs on 2026-10-16; the bar is 100 of 100. See [below](#keys-typed-right-after-cmdt). |

## Keys typed right after Cmd+T

`MANUAL_KeysTypedRightAfterCmdTLand` posts Cmd+T and "github" through the app's event queue in a real window. The panel shows and becomes the key window at once, but the field stays empty: every key arrives before the page can take it.

A temporary build waited a set time between Cmd+T and the first key, three runs each:

| Gap after Cmd+T | Result |
| --- | --- |
| 0, 5, 10, 20 ms | 0 of 3: every key lost |
| 40 ms | 1 of 3 |
| 80 ms | 3 of 3 |

The user typed by hand right after Cmd+T and every letter landed. A fast typist can still press the first key inside 40 ms, so uw holds keys in the browser, as the plan's [design](../plans/command-bar.md#design) says. The test stays as the check for that, and should pass 100 of 100 runs.

### Holding keys

Built on 2026-10-15 in `browser/ui/views/command_bar/command_bar.cc`. From Cmd+T, the page's `PreHandleKeyboardEvent` keeps each key down instead of sending it. When the page reports a focused field, the browser sends each held key down and its character, as macOS would have. If the browser refuses a key, that key and the ones after it stay held and are tried again a frame later. After one second the browser stops holding.

A key reaches the field only when both of these are true:

- **The field has focus.** The page focuses it one task after the window's `focus` event. During that event, Blink has not yet told the browser which frame has focus, so the browser ignores the field's focus (`FocusController::SetFocusedFrame`, `WebContentsImpl::OnFocusedElementChangedInFrame`).
- **The browser accepts input for the page.** After a page loads, the browser refuses its input until the page's first frame is on screen. `RenderInputRouter::FilterInputEvent` answers `kNoConsumerExists` while the input router is off, and `RenderWidgetHostImpl::OnRenderFrameMetadataChangedAfterActivation` turns it on. The command bar's page loads hidden, so the first time the panel shows, its keys are refused until its first frame, some tens of milliseconds after Cmd+T. That matches the gaps above. A refused key comes back at once, and `RenderWidgetHost::InputEventObserver` sees it.

What the work found, in the order it was tried:

1. **Focusing the field in the window's `focus` event.** Failed 100 of 100 real-window runs at 800 ms. The browser never heard of the focus, and the keys landed only when the one-second fallback sent them.
2. **Focusing it one task later.** Failed one real-window run.
3. **Focusing it on the next animation frame.** Tried on the theory that Blink drops input until a shown page draws. Failed 3 of 3. Blink drops input itself (`WidgetInputHandlerManager`) only around a page load, so the theory was wrong.
4. **Logging a real-window run.** In 3 runs, all six keys were held, the page reported its focused field about 12 ms after Cmd+T, and the browser refused each key as it was sent. The page saw no key events at all.
5. **Trying refused keys again a frame later.** Passed 10 of 10 real-window runs on 2026-10-16, each opening the panel twice: once before its page had drawn and once after. A 100-run check then passed 97: two runs had an empty field at 800 ms, one of them while another session built and ran tests, and one typed "githubg", a letter twice. Not yet explained.

Also found:

- **Test keys skip the browser's hook.** `content::SimulateKeyPress` marks its keys `skip_if_unhandled`, so they never reach `PreHandleKeyboardEvent`. That is also why Escape sent that way never closed the panel.
- **Headless cannot check it.** A headless panel never draws, so the browser never accepts input for its page. A headless test of the hold was written and removed.
- **Real-window runs are the user's.** A 100-run loop in a real window on 2026-10-15 put windows on the user's screen for four minutes while they worked. A hook now blocks `--headful-mode` and `--run-manual` for agents. Every real-window check since was a short run the user started.

## What the spike found

- **Keeping the page ready.** Chromium's `WebUIContentsPreloadManager` keeps one page loaded for all of Chromium and picks which one. Tab search's `WebUIBubbleManager` drops its page 30 seconds after the bubble closes. Neither keeps the command bar ready, so each window owns a `WebUIContentsWrapper` for as long as the setting is on, and shows it in a `WebUIBubbleDialogView`.
- **Ready to show.** The wrapper counts a page as ready only when the preload manager made it. The command bar shows its bubble at once and does not wait for the page to say it is ready.
- **Suggestions.** `RealboxHandler`, the new tab page's handler, works as it is with no contextual search session. Picking a match would load it inside the panel, because its omnibox client navigates the page that hosts it. T61 needs a handler of uw's own to open pages.
- **WebUI name list.** A top-chrome page's name must be in the `WebUIName` variants of `tools/metrics/histograms/metadata/page/histograms.xml`, or `WebUIContentsWrapperT` fails a compile-time check. `uw-terminal.patch` adds `.UwCommandBar`.
- **Headless keyboard tests.** A headless panel never takes focus, so its page drops keys. A key that nobody handles goes back to the window and into the page again until AppKit aborts in `-[NSView performKeyEquivalent:]`. Escape sent with `content::SimulateKeyPress` never reaches the wrapper's Escape handling. The two keyboard tests are `MANUAL_` tests that post keys through the app's event queue. Run them with `--run-manual --headful-mode`.

## Still open

- `MANUAL_KeysTypedRightAfterCmdTLand` in 100 of 100 real-window runs, run by the user. 97 of 100 passed on 2026-10-16. Why a letter was typed twice, and why two runs stayed empty.
- The 100 ms target from Cmd+T to the panel on screen. Not measured.
