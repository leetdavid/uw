// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_LIVE_PILL_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_LIVE_PILL_H_

#include <memory>
#include <string>

#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"

namespace content {
class WebContents;
}

namespace uw {

// The Live toggle of a site's own picture-in-picture window, per "Live
// windows" in docs/plans/mini-player.md: a pill reading "Live" after a dot,
// green while the site's windows are live and hollow while not. A click
// changes the choice saved for the site, which the settings page lists too.
class LivePill : public views::Button {
  METADATA_HEADER(LivePill, views::Button)

 public:
  enum class Look {
    // White over the page, on a dark pill, as the mini player's controls.
    kOverPage,
    // In the floating window's bar, in the bar's text color.
    kBar,
  };

  // The pill for the site of `opener`, the tab that opened the window.
  static std::unique_ptr<LivePill> ForOpener(content::WebContents* opener,
                                             Look look);

  LivePill(std::string site, Look look);
  LivePill(const LivePill&) = delete;
  LivePill& operator=(const LivePill&) = delete;
  ~LivePill() override;

  bool live() const;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void Toggle();
  void OnChanged();

  const std::string site_;
  const Look look_;
  PrefChangeRegistrar prefs_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_LIVE_PILL_H_
