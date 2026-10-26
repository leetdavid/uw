# Mini player

Status: scope agreed on 2026-10-24 in a grilling session. The design, T185, was drawn and signed off the same day. T186, the floating mini player, was built the same day; T187, docking in the sidebar, T188, automatic mini players, T189, the stack, and T190, document picture in picture's frame, on 2026-10-25. All five are in review. A second round was agreed on 2026-10-25 in another grilling session: sites' own windows dock as [live windows](../CONTEXT.md#language), [playback controls](../CONTEXT.md#language) join the sidebar, and one choice replaces the first switch. The design, T203, was signed off the same day. T204's prototype showed a site's page can move into the sidebar, so T207 goes ahead as planned. T205, the Dock, Don't dock, and Off choice, T206, the playback controls, T207, sites' own windows in the sidebar, and T208, Live per site, were built the same day and are in review. On 2026-10-26 the user found the video moving out of the sidebar and back each time they switched apps: "every time i change focus, the video pops in and out". In a grilling session the same day they chose that it float only when it would be hidden, T214, at **5/10**.

Implementability: **4/10** for the first round, using the [roadmap convention](roadmap.md#implementability-estimates). Chromium lets the browser supply its own picture-in-picture window, and hands that window the video as a compositor surface that any view can show. Its controller already plays, pauses, seeks, mutes, and goes back to the tab, and its media session can start picture in picture on a playing video without the page's help. The cost is in what uw adds: one live video that moves between sidebars and a floating window, a stack of paused videos with stills, a 2× speed that Chromium has no call for, and controls that follow the Glass slider.

**3/10** for the second round. Chromium keeps one picture-in-picture window at a time, and gives a site's own window a browser window of its own. Showing that page in the sidebar beside a video needs a deeper patch than anything in the first round, and a prototype, T204, must first show that the page keeps working there.

The user asked on 2026-10-24 for picture-in-picture videos to show at the bottom of the sidebar, so they can browse other tabs while the video plays, and to float in their own window while uw is out of focus. They also asked for Arc's controls on hover, and for uw's own floating window to replace Chromium's, with rounded corners and glass. uw calls it the [mini player](../CONTEXT.md#language), as Arc does. On 2026-10-25 they asked to see how a Google Meet call would look in the sidebar, and then for small sites' windows to dock too: "we may only want to support pips of a certain size (videos audios and other small pips)".

## Agreed scope

### Where it shows

- Every request for picture in picture opens a mini player, unless the [setting](#settings) says otherwise: the site's own button, an extension, and Picture in Picture in the video's right-click menu.
- It docks at the bottom of the sidebar, above the foot, across the sidebar's width. The tab list ends above it.
- From the top down, the bottom of the sidebar holds the activity card, the mini player and its stack, a [live window](#live-windows), the [playback controls](#playback-controls), and the foot, each 6px above the next.
- When the window is too short for all of them, the tab list scrolls, as it does when it is long.
- It docks in the sidebar of whichever uw window is in front, so it follows the user across windows and [workspace](../CONTEXT.md#language) switches.
- It becomes a [floating mini player](../CONTEXT.md#language) when it would be hidden: when no uw window is showing, when the front window's sidebar is [hidden](../CONTEXT.md#language) or in full screen, or when, with another app active, that app's windows have covered half or more of the docked mini player for about a second. It stays floating while the [floating sidebar](../CONTEXT.md#language) shows. Changed on 2026-10-26: before, it floated whenever another app was active.
- With another app active and the sidebar in view, it stays docked, so uw beside another app keeps the video where it is.
- Other apps' ordinary windows cover it. The menu bar, the Dock, notification banners, and the mini player's own window do not. A uw window on another Space counts as covered.
- Once floating, it stays floating until the user comes back to uw, even when the covering window moves away.
- It docks again when a uw window is active with its sidebar showing. Clicking or dragging the floating mini player does not count as coming back.

### Starting and ending

- With Dock chosen, switching away from a tab starts a mini player for its video when the video is playing, not muted, and large enough on the page, as Arc skips muted tabs. The video must be the page's only one, as for Chrome's media controls; a page with several players starts no mini player on its own.
- uw does not ask the site for permission first, as Chromium does. The setting is the permission.
- Selecting the video's tab puts a mini player that started this way back in its page.
- A mini player the user started stays until it is closed, since the user was on its tab when they asked.
- Closing the video's tab closes its mini player.

### The stack

- A second mini player pauses the first and docks on top of it, as a [mini player stack](../CONTEXT.md#language). The stack holds up to three. A fourth drops the oldest.
- The paused ones peek out a few pixels above the playing one. Hovering slides them up from behind it into rows, each a still of its last frame with its site's icon and title. When the pointer leaves, they slide back down. With reduced motion they move without sliding.
- Clicking a paused one plays it on top and pauses the one it replaces, which goes under it.
- Each has its own close. Going to a paused video's tab takes it out of the stack.
- Only the playing one floats. The paused ones come back when it docks again.
- A live window never joins it.
- The stack lives only in the sidebar. With Don't dock, or for a site's window too big to dock, a new picture in picture closes the old one and leaves it paused in its page, as Chromium does.

### Controls on hover

- Play or pause in the middle, between back 10 seconds and forward 10 seconds.
- A progress bar along the bottom with the time. Dragging it seeks.
- Close in one top corner and back to tab in the other. Close stops the mini player and leaves the video paused in its page.
- Mute in a bottom corner.
- Skip ad, next and previous track, and a call's microphone, camera, and hang up show when the site offers them, as in Chromium's window.
- Pressing and holding on the video plays it at 2× while held. Letting go puts back the speed it had. In the floating mini player, moving the pointer while pressed drags the window instead.

### Look

- Rounded corners, docked and floating, in the [Arc-style look](arc-style-look.md).
- The controls follow the theme's [Glass](themes.md#glass) slider: flat at its left end, lit glass as it moves right, like the foot's buttons. The floating mini player uses the slider of the last active window's workspace.
- uw's floating mini player replaces Chromium's picture-in-picture window for every video.

### Sites' own windows

Some sites open a small window of their own page rather than a video, such as Google Meet calls and Spotify's web player.

- One docks when the site asks for a window up to 480 × 480 px. A bigger one floats. Before shipping, the limit is checked against the sizes Meet and Spotify ask for, and raised to fit them if needed.
- Docked, it is the sidebar's width and the shape the site asked for, never taller than it is wide. The site lays its page out at that width and draws its own controls.
- Under the pointer, uw adds close and back to tab in the top corners, as on a video, a Live toggle, and no bar. Back to tab is hidden when the site asks for that.
- Floating, it has the floating mini player's rounded frame and a 30px bar in the tint of the window it came from, with back to tab, close, and the Live toggle.
- A site can resize its window only right after the user clicks or types in it. Within the limit, a docked one stays docked and takes the new shape. Past the limit it floats at the size asked for, and stays floating until it closes.
- While Chromium's prompt for automatic picture in picture shows in it, it floats. It docks once the user answers.
- Selecting its tab leaves it where it is, as with a video the user put in picture in picture. The site can still close it.
- It floats and docks by the same rules as a video. With a video docked above it, both float: the site's window in the corner and the video just above it.

### Live windows

- Every site's own window is a [live window](../CONTEXT.md#language) by default: it never pauses and never joins the stack. Docked, it sits under the mini player, which docks above it.
- One live window at a time. A newer site's window takes its place, and the older one goes back to its tab.
- The user picks which sites' windows are live, on the settings page and with the Live toggle on the window. Both change the same saved choice for that site.
- A site's window that is not live acts like a mini player: when another picture in picture comes, it pauses and joins the stack with a still.
- Turning Live off while it is docked under a playing video pauses it and puts it in the stack under that video.
- uw's choice, not yet agreed: when the video above it closes, a site's window waiting in the stack plays again, since nothing else would bring it back.

### Playback controls

- When a tab the user is not looking at plays sound, the sidebar shows [playback controls](../CONTEXT.md#language) for it just above the foot: play or pause, and next and previous when the site offers them. This holds whatever the setting.
- One row, for the tab that started playing last. Paused sound keeps the row until another tab plays or the tab closes. The others stay in Chromium's media button in the toolbar.
- Sound in a mini player or a live window shows no row, including the sound of the tab that opened a live window. Those have their own controls.

### Settings

The Mini player card on uw's settings page holds:

1. A choice, "Dock in the sidebar" by default:
   - Dock in the sidebar: as above.
   - Don't dock: nothing docks. Leaving a playing video's tab opens nothing, and the playback controls show for it. The site's picture-in-picture button and the menu still float a video. Sites' own windows float.
   - Off: no picture in picture at all. Videos stay in their pages, and sites cannot open their own windows.
2. "Open the mini player when you switch tabs", on by default. Grayed out unless Dock is chosen.
3. "Float the mini player when the sidebar is covered", on by default. Grayed out unless Dock is chosen. Off, nothing floats while a uw window shows. Before 2026-10-26 it read "Float the mini player when uw is in the background".
4. Every site that has opened its own window, each with a Live switch, on by default.

The choice replaces the switch "Show the mini player in the sidebar". Its off state, where every mini player floats, goes away. T203 settles the wording.

## Non-goals

- A per-site switch for automatic mini players.
- A button to float a docked mini player by hand.
- Several live windows at once.
- More than one row of playback controls.
- A stack outside the sidebar.

## Design

The first round was drawn and signed off on 2026-10-24, T185, on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Keyboard and mouse", beside the floating sidebar. The numbers are in the canvas's `uw-mini-player` guide.

- "Mini player: the sidebar": a docked mini player under the pointer, with its controls, in light mode.
- "Mini player: floating": the floating mini player over another app, with Glass on.
- "Mini player: every state": at rest and under the pointer with Glass at both ends, holding for 2×, black dark mode, the stack at rest and fanned out, a tall video, the activity card above it, a skip ad button, floating at rest, a call, document picture in picture's frame, and the settings.

uw's proposals in the frames:

- Docked, it is 232px wide with 10px corners, 6px above the foot, and never taller than it is wide. A 9:16 video is 232px tall and 131px wide, centered.
- At Glass's left end each control is a black circle at 40%. Over video the glass stays dark, so the white icons read over any frame.
- Holding for 300 ms without moving starts 2×. The other controls go, and a "2×" pill shows at the top.
- The activity card shows above the mini player, so the video never moves.
- Fanned out, each paused video is a row with a still, its title, and where it stopped.

The second round, T203, is on the same page and not signed off yet:

- "Mini player: a Meet call in the sidebar": a Meet call docked under the pointer, in light mode.
- "Sites' own windows in the sidebar: every state": a call at rest, a group call, an audio player, black dark mode under the pointer, the size rule, and a video or a stack above a call.
- "Mini player second round: every state": the playback controls at rest, under the pointer, paused, in black dark mode, and with Don't dock; everything docked at once; the Live pill on and off; a site's window that is not live in the stack; the Live pill on a floating site's window; and the settings card with Dock and with Don't dock chosen.

uw's proposals in these frames:

- The playback row is 44px, in the tint's fill, with art or the site's icon, the title, where it comes from, and play or pause, with previous and next whenever the site offers them. The row looks the same under the pointer, so its buttons never move: on 2026-10-26 the user found previous and next appearing on hover "jarring" and asked for "the button should always show instead". Clicking the art or title goes to the tab.
- The Live toggle is a pill reading "Live" at the top center of a docked site's window, between close and back to tab, and in the floating bar before them. Its dot is green when on and hollow when off.
- The settings card lists the Live sites newest first.

Meet's and Spotify's layouts at 232px in these frames are guesses; each site draws its own.

## How it is built

Product code goes in `browser/ui/views/mini_player/`, per [Architecture](../architecture.md#code-organization).

- A patch makes `content::VideoOverlayWindow::Create` return uw's mini player in place of `VideoOverlayWindowViews`. It shows the surface Chromium hands it in a view, and moves that view between the front window's sidebar and uw's floating widget. The video keeps playing through the move, since the surface does not change.
- The controls call the existing `VideoPictureInPictureWindowController` methods: play and pause, `SeekTo`, mute, back to tab, close.
- Automatic mini players come from a watcher on each window's tabs. On a switch, it asks the left tab's media session for picture in picture, as Chrome's media controls do, when the setting is on and the video plays with its sound and is big enough. "Big enough" is Blink's own measure: at least 10,000 square pixels of the video in the viewport and not covered, which Blink only takes with `AutoPictureInPictureVideoHeuristics`, turned on in `uw-trials.patch`. Chromium's own automatic picture in picture is left as it is: it needs HTTPS, a site the user watches often, a permission prompt, and no other mini player open.
- The stack keeps, for each paused video, its tab and a still: the mini player copies its video every two seconds while it plays. Chromium hides the old mini player and shows the new one in one go, which tells a replaced video from a closed one.
- 2× runs a short script in an isolated world in the video's frame that sets the picture-in-picture video's `playbackRate` and puts it back on release. Chromium has no browser-side call for playback speed.
- Document picture in picture keeps Chromium's window, with macOS's own rounded corners and shadow and no lit edge. `uw-mini-player.patch` makes its bar 30px and paints it with the tint of the window the page came from. `ThemeOfWindow` finds that window through the tab that opened the picture in picture.

The second round:

- Chromium keeps one picture-in-picture window at a time: `PictureInPictureWindowManager` closes the old window when a new one opens. `uw-mini-player.patch` lets it keep a site's own window beside a video. A video no longer closes it; it waits as the manager's kept window, and is the window again once the video closes. A newer site's window closes it, and when it closes on its own the video stays.
- Chromium gives a site's own window a browser window of its own with one tab (`NEW_PICTURE_IN_PICTURE` in `browser_navigator.cc`). Docking keeps that window hidden and shows its page in the sidebar, as uw already moves a pinned tab's live page between windows. uw's `SiteWindow` follows the window Chromium opens, docks and floats by the mini player's rules, and swaps the page out of its window with a blank page in its place. Docked, the page shows in a `views::WebView` under any video, clipped to rounded corners by its layer, with close and back to tab drawn over it. While docked, uw is the page's delegate, so its close, its `resizeTo`, and its keys reach uw. Chromium makes a new site's window the active window; that does not count as the user clicking it. T204's prototype, a throwaway browser test, did this with `TabStripModel::DiscardWebContents` and a `views::WebView` at 232 × 196 on 2026-10-25. The page took the view's size and heard a resize, stayed the site's picture-in-picture window, took typing, went back to its own window at its own size, and closed through back to tab as Chromium does. Closed while out of its window, the page asks its delegate to close it, which is then uw, and destroying it ends the window for the site. Not tried: uw's buttons drawn over the page.
- A video moved into a site's window keeps its player in the page that opened it (`ShouldReusePlayer` in Blink's `html_media_element.cc`), and Spotify plays from its main page. So pausing a site's window that is not live pauses the media session of the tab that opened it, as the stack pauses videos.
- The playback controls follow the window's tab that last gained audio focus, from Chromium's audio focus manager, and read that tab's media session for its title, artwork, state, and actions, the data behind Chromium's media button. The row draws itself, as the stack's rows do. Each window's row is for that window's own tabs.
- The Live choice of each site is a list in local state, newest first, which the settings page lists and the Live pill changes. A site's window that is not live waits in the stack as an entry whose play and drop run the window's own resume and close; playing it closes the video's picture in picture and keeps the video paused in the stack. `uw-mini-player.patch` adds the pill to the floating window's bar, and sends that bar's close and back to tab, and its title, to the kept site's window rather than the video above it.
- Whether the dock is covered comes from macOS's list of the windows above uw's window, `CGWindowListCopyWindowInfo`, which gives other apps' window bounds without screen-recording permission; only their titles need it. A window macOS says is not visible, such as one on another Space, counts as covered. While another app is active and something is docked, the dock checks twice a second. It keeps the covered state, so the video and a site's window float together, and clears it when uw comes to the front.
- Off holds Chromium's own `ScopedDisallowPictureInPicture` in every window, as a file dialog does, which closes a video's picture in picture at once. `uw-mini-player.patch` makes the browser answer content's `IsDocumentPictureInPictureBlockedBySystem` with that block, so a site's request for its own window is refused.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T185: the designs, signed off by the user.
2. T186: uw's floating mini player in place of Chromium's window, with the controls, the Glass look, and 2×.
3. T187: docking in the front window's sidebar, floating in the background and with the sidebar hidden, and the two settings for those.
4. T188: automatic mini players on a tab switch, back into the page on return, and their setting.
5. T189: the stack.
6. T190: document picture in picture's frame.
7. T203: the second round's designs, signed off by the user.
8. T204: a throwaway prototype of a site's page in the sidebar. Not merged.
9. T205: the Dock, Don't dock, and Off choice.
10. T206: the playback controls.
11. T207: sites' own windows docked as live windows, beside a video.
12. T208: Live per site, on the settings page and the window.
13. T214: the mini player floats only when it would be hidden.

## Acceptance checks

1. A video put in picture in picture docks at the bottom of the sidebar, keeps playing while the user switches tabs, and its controls play, pause, seek, mute, go back to the tab, and close.
2. Holding on the video plays it at 2×, and letting go restores the speed it had.
3. Switching to another uw window or workspace moves it to that window's sidebar. Hiding the sidebar floats it, and showing the sidebar docks it.
4. With another app active and the sidebar in view, it stays docked. Once that app's window covers half the docked video for a second, it floats, and it stays floating when the window moves away; coming back to uw docks it. A window covering less than half leaves it docked. With "Float the mini player when the sidebar is covered" off, it stays docked.
5. Switching away from a tab with a playing, audible, visible video starts a mini player. A muted or paused video, or a small one, does not. Selecting the tab puts it back in the page.
6. A second mini player pauses the first and stacks on it. Clicking the paused one swaps them. A fourth drops the oldest.
7. With Don't dock, leaving a playing video's tab opens nothing and the playback controls show it, and the site's picture-in-picture button floats it. With Off, no picture in picture opens, and a site cannot open its own window.
8. The controls match the frames with Glass at both ends, in light and dark mode. Document picture in picture has the rounded frame.
9. `pnpm run check` and the focused browser tests pass. Browser tests stay headless; checks that need another app in front or a real window run as `MANUAL_` tests in the window-tests VM.
10. A Meet call docks at the bottom of the sidebar and keeps running while the user switches tabs. A video put in picture in picture docks above it, and neither pauses.
11. With another app's window covering the sidebar, the call floats in the corner and the video just above it, and both dock again when uw comes back.
12. A site's window bigger than 480 × 480 floats. One the site grows past that floats and stays floating.
13. A second site's window takes the live window's place, and the first goes back to its tab.
14. With Live off for a site, its window pauses and joins the stack when a video comes. Turning Live off on the window while a video plays above it puts it in the stack under the video.
15. A background tab playing sound shows one row of playback controls above the foot, for the tab that started last, and its buttons play, pause, and skip. A video in a mini player and a live window's tab show none.

## Open questions

- The sizes Meet and Spotify ask for, and how their pages lay out at 232px.
