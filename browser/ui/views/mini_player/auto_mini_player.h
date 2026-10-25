// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_AUTO_MINI_PLAYER_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_AUTO_MINI_PLAYER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace uw {

// Opens a mini player when the user switches away from a tab whose video is
// playing, audible, and big enough on the page, and puts the video back in
// its page when they come back to that tab, per "Starting and ending" in
// docs/plans/mini-player.md, while the mini player docks. A mini player the
// user opened stays.
class AutoMiniPlayer : public TabStripModelObserver {
 public:
  explicit AutoMiniPlayer(BrowserWindowInterface* window);
  AutoMiniPlayer(const AutoMiniPlayer&) = delete;
  AutoMiniPlayer& operator=(const AutoMiniPlayer&) = delete;
  ~AutoMiniPlayer() override;

  // True when uw opened the mini player of `contents` on a tab switch.
  static bool OpenedOnTabSwitch(content::WebContents* contents);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void MaybeOpen(content::WebContents* contents);
  void PutBack(content::WebContents* contents);

  const raw_ptr<BrowserWindowInterface> window_;
  base::WeakPtrFactory<AutoMiniPlayer> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_AUTO_MINI_PLAYER_H_
