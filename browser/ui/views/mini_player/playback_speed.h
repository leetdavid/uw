// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_SPEED_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_SPEED_H_

namespace content {
class WebContents;
}

namespace uw {

// Plays the picture-in-picture video of `contents` at 2× while `fast`, and
// puts back the speed it had once it is not. Chromium has no browser-side call
// for a video's speed, so a script in an isolated world sets it, out of the
// page's reach.
void SetPlaybackFast(content::WebContents* contents, bool fast);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_SPEED_H_
