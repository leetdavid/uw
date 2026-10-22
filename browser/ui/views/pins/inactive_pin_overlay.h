// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_PINS_INACTIVE_PIN_OVERLAY_H_
#define UW_BROWSER_UI_VIEWS_PINS_INACTIVE_PIN_OVERLAY_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class BrowserWindowInterface;

namespace uw {

class HazeView;

// Covers the page of a window whose selected pin has its live page in another
// window, per the design frame "Pins: the live page in another window": a
// blurred, dimmed picture of the page as it left, with "Open here" in the
// middle. A click anywhere on it brings the live page here. Owned by
// EmbedderBrowserWindowFeatures.
class InactivePinOverlay : public TabStripModelObserver,
                           public views::ViewObserver {
 public:
  DECLARE_USER_DATA(InactivePinOverlay);

  explicit InactivePinOverlay(BrowserView* browser_view);
  InactivePinOverlay(const InactivePinOverlay&) = delete;
  InactivePinOverlay& operator=(const InactivePinOverlay&) = delete;
  ~InactivePinOverlay() override;

  static InactivePinOverlay* From(BrowserWindowInterface* window);

  // The overlay while it shows, or null.
  views::View* GetShownView();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void Update();
  void BringPage();

  HazeView* haze();

  const raw_ptr<BrowserView> browser_view_;
  views::ViewTracker haze_;
  base::ScopedObservation<views::View, views::ViewObserver> contents_{this};
  base::CallbackListSubscription pictures_;
  ui::ScopedUnownedUserData<InactivePinOverlay> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_PINS_INACTIVE_PIN_OVERLAY_H_
