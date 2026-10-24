// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_STACK_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_STACK_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "ui/gfx/image/image_skia.h"

namespace content {
class WebContents;
}

namespace uw {

// The paused videos under the playing mini player, per "The stack" in
// docs/plans/mini-player.md. Chromium shows one picture in picture at a time,
// so a new one hides the old one at once; the old video pauses and waits here,
// newest first, until the user plays it again, drops it, or goes to its tab.
class MiniPlayerStack {
 public:
  // A paused video.
  struct Entry {
    Entry();
    Entry(const Entry&);
    Entry& operator=(const Entry&);
    ~Entry();

    base::WeakPtr<content::WebContents> contents;
    std::u16string title;
    // Its last frame, or the site's icon when no frame was caught.
    gfx::ImageSkia still;
    gfx::ImageSkia favicon;
    base::TimeDelta paused_at;
  };

  // Two paused under the playing one: three in all.
  static constexpr size_t kMaxPaused = 2;

  static MiniPlayerStack& Get();

  MiniPlayerStack(const MiniPlayerStack&) = delete;
  MiniPlayerStack& operator=(const MiniPlayerStack&) = delete;

  // The mini player of `contents` was hidden, showing `still` and at
  // `position`. If another mini player shows before this task ends, it
  // replaced this one, and this video pauses under it.
  void OnHidden(content::WebContents* contents,
                gfx::ImageSkia still,
                base::TimeDelta position);
  // The mini player of `contents` showed.
  void OnShown(content::WebContents* contents);

  const std::vector<Entry>& paused() const { return paused_; }

  // Plays the paused video at `index` in the mini player again. The one
  // playing now goes under it.
  void Play(size_t index);
  // Drops the paused video at `index`. It stays paused in its page.
  void Drop(size_t index);
  // Drops the video of `contents`, as when the user goes to its tab.
  void Remove(content::WebContents* contents);

  base::CallbackListSubscription Observe(base::RepeatingClosure callback);

 private:
  friend class base::NoDestructor<MiniPlayerStack>;

  MiniPlayerStack();
  ~MiniPlayerStack();

  void ForgetHidden();

  std::optional<Entry> hidden_;
  std::vector<Entry> paused_;
  base::RepeatingClosureList observers_;
  base::WeakPtrFactory<MiniPlayerStack> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_STACK_H_
