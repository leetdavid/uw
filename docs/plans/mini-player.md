# Mini player

Status: scope agreed on 2026-10-24 in a grilling session. The design, T185, is drawn and waits for the user's sign-off. Nothing is built.

Implementability: **4/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium lets the browser supply its own picture-in-picture window, and hands that window the video as a compositor surface that any view can show. Its controller already plays, pauses, seeks, mutes, and goes back to the tab, and its media session can start picture in picture on a playing video without the page's help. The cost is in what uw adds: one live video that moves between sidebars and a floating window, a stack of paused videos with stills, a 2× speed that Chromium has no call for, and controls that follow the Glass slider.

The user asked on 2026-10-24 for picture-in-picture videos to show at the bottom of the sidebar, so they can browse other tabs while the video plays, and to float in their own window while uw is out of focus. They also asked for Arc's controls on hover, and for uw's own floating window to replace Chromium's, with rounded corners and glass. uw calls it the [mini player](../CONTEXT.md#language), as Arc does.

## Agreed scope

### Where it shows

- Every request for picture in picture opens a mini player: the site's own button, an extension, and Picture in Picture in the video's right-click menu.
- It docks at the bottom of the sidebar, above the foot, across the sidebar's width. The tab list ends above it.
- It docks in the sidebar of whichever uw window is in front, so it follows the user across windows and [workspace](../CONTEXT.md#language) switches.
- It becomes a [floating mini player](../CONTEXT.md#language) when another app is active, when no uw window is showing, or when the front window's sidebar is [hidden](../CONTEXT.md#language) or in full screen. It stays floating while the [floating sidebar](../CONTEXT.md#language) shows.
- It docks again when a uw window is active with its sidebar showing. Clicking or dragging the floating mini player does not count as coming back.

### Starting and ending

- Switching away from a tab starts a mini player for its video when the video is playing, not muted, and large enough on the page, as Arc skips muted tabs. Chromium's automatic picture in picture also needs the video in the page's own frame, as the only player, on a page not using the camera or microphone; a video embedded from another site docks only when the user asks.
- uw does not ask the site for permission first, as Chromium does. The setting is the permission.
- Selecting the video's tab puts a mini player that started this way back in its page.
- A mini player the user started stays until it is closed, since the user was on its tab when they asked.
- Closing the video's tab closes its mini player.

### The stack

- A second mini player pauses the first and docks on top of it, as a [mini player stack](../CONTEXT.md#language). The stack holds up to three. A fourth drops the oldest.
- The paused ones peek out a few pixels above the playing one. Hovering fans them out upward, each a still of its last frame with its site's icon and title.
- Clicking a paused one plays it on top and pauses the one it replaces, which goes under it.
- Each has its own close. Going to a paused video's tab takes it out of the stack.
- Only the playing one floats. The paused ones come back when it docks again.

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

### Document picture in picture

Some sites open a small window of their own page rather than a video, such as Google Meet calls and Spotify's web player. Those keep floating and never dock, but get the floating mini player's rounded Arc frame. The site draws its own controls.

### Settings

Three switches on uw's settings page, all on by default:

1. "Show the mini player in the sidebar". Off, it always floats.
2. "Open the mini player when you switch tabs".
3. "Float the mini player when uw is in the background". Grayed out while the first is off.

## Non-goals

- Arc's audio controller at the bottom of the sidebar for tabs playing sound without video.
- A per-site switch for automatic mini players.
- A button to float a docked mini player by hand.
- Docking document picture in picture.

## Design

To be drawn for sign-off, T185, on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Keyboard and mouse", beside the floating sidebar:

- "Mini player: the sidebar": a docked mini player under the pointer, with its controls, in light mode.
- "Mini player: floating": the floating mini player over another app.
- "Mini player: every state": at rest and under the pointer, holding for 2×, the stack at rest and fanned out, a tall video, Glass at both ends, dark mode, document picture in picture's frame, and the three settings.

The design proposes the docked size, the height cap for tall videos, the flat controls at the Glass slider's left end, and the 2× label.

## How it is built

Proposed. Product code goes in `browser/ui/views/mini_player/`, per [Architecture](../architecture.md#code-organization).

- A patch makes `content::VideoOverlayWindow::Create` return uw's mini player in place of `VideoOverlayWindowViews`. It shows the surface Chromium hands it in a view, and moves that view between the front window's sidebar and uw's floating widget. The video keeps playing through the move, since the surface does not change.
- The controls call the existing `VideoPictureInPictureWindowController` methods: play and pause, `SeekTo`, mute, back to tab, close.
- Automatic mini players use Chromium's browser-initiated automatic picture in picture, `kBrowserInitiatedAutomaticPictureInPicture`, gated by uw's setting and the muted and size checks, without Chromium's permission prompt.
- The stack keeps, for each paused video, its tab and a copy of the last frame taken before the surface goes.
- 2× runs a short script in an isolated world in the video's frame that sets the picture-in-picture video's `playbackRate` and puts it back on release. Chromium has no browser-side call for playback speed.
- Document picture in picture's frame gets its corners and edge in the patch that draws that window's frame.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T185: the designs, signed off by the user.
2. T186: uw's floating mini player in place of Chromium's window, with the controls, the Glass look, and 2×.
3. T187: docking in the front window's sidebar, floating in the background and with the sidebar hidden, and the three settings.
4. T188: automatic mini players on a tab switch, and back into the page on return.
5. T189: the stack.
6. T190: document picture in picture's frame.

## Acceptance checks

1. A video put in picture in picture docks at the bottom of the sidebar, keeps playing while the user switches tabs, and its controls play, pause, seek, mute, go back to the tab, and close.
2. Holding on the video plays it at 2×, and letting go restores the speed it had.
3. Switching to another uw window or workspace moves it to that window's sidebar. Hiding the sidebar floats it, and showing the sidebar docks it.
4. With another app active, it floats; coming back to uw docks it. With "Float the mini player when uw is in the background" off, it stays docked.
5. Switching away from a tab with a playing, audible, visible video starts a mini player. A muted or paused video, or a small one, does not. Selecting the tab puts it back in the page.
6. A second mini player pauses the first and stacks on it. Clicking the paused one swaps them. A fourth drops the oldest.
7. With "Show the mini player in the sidebar" off, every mini player floats in uw's window.
8. The controls match the frames with Glass at both ends, in light and dark mode. Document picture in picture has the rounded frame.
9. `pnpm run check` and the focused browser tests pass. Browser tests stay headless; checks that need another app in front or a real window run as `MANUAL_` tests in the window-tests VM.

## Open questions

None.
