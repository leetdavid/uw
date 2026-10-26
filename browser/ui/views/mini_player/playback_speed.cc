// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/playback_speed.h"

#include "base/functional/callback_helpers.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace uw {
namespace {

// Each frame looks for its own picture-in-picture video; only one has it. The
// speed it had is kept on the video, where only this world can see it.
constexpr char16_t kFast[] =
    uR"((() => {
  const video = document.pictureInPictureElement;
  if (!video) return;
  if (video.uwSpeed === undefined) video.uwSpeed = video.playbackRate;
  video.playbackRate = 2;
})())";

constexpr char16_t kNormal[] =
    uR"((() => {
  const video = document.pictureInPictureElement;
  if (!video || video.uwSpeed === undefined) return;
  video.playbackRate = video.uwSpeed;
  delete video.uwSpeed;
})())";

}  // namespace

void SetPlaybackFast(content::WebContents* contents, bool fast) {
  if (!contents) {
    return;
  }
  contents->ForEachRenderFrameHost([fast](content::RenderFrameHost* frame) {
    if (frame->IsRenderFrameLive()) {
      frame->ExecuteJavaScriptInIsolatedWorld(
          fast ? kFast : kNormal, base::NullCallback(),
          ISOLATED_WORLD_ID_CHROME_INTERNAL);
    }
  });
}

}  // namespace uw
