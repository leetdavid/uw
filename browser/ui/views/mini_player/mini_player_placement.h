// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PLACEMENT_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PLACEMENT_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace uw {

class MiniPlayerDock;

// Where the mini player goes, per "Where it shows" in
// docs/plans/mini-player.md: docked in the sidebar of its home window, or
// floating in its own window while no uw window shows, the home window is in
// front with its sidebar hidden, or, with another app active, that app's
// windows cover the dock, and always floating with Don't dock chosen. A home
// window out of sight behind another uw window keeps it docked there. It
// watches all of those and the settings, and says again whenever one changes.
class MiniPlayerPlacement : public BrowserCollectionObserver,
                            public views::WidgetObserver {
 public:
  // Called with the dock to go into, or nullptr to float.
  using PlaceCallback = base::RepeatingCallback<void(MiniPlayerDock*)>;

  // `floating_window` is the mini player's own window: a click on it brings uw
  // to the front without coming back to a browser window. `tab` gives the
  // tab whose window is the home window when none is set.
  MiniPlayerPlacement(views::Widget* floating_window,
                      base::RepeatingCallback<content::WebContents*()> tab,
                      PlaceCallback place);
  MiniPlayerPlacement(const MiniPlayerPlacement&) = delete;
  MiniPlayerPlacement& operator=(const MiniPlayerPlacement&) = delete;
  ~MiniPlayerPlacement() override;

  // Says where the mini player goes now.
  void Update();
  // Says it for a window that just opened and took the focus as it did, which
  // is not the user coming back to it.
  void UpdateNew();
  // Says it a moment from now, once a burst of changes, such as a workspace
  // switch hiding one window and showing another, has settled.
  void ScheduleUpdate();

  // The window it docks in. Once that closes, its tab's window.
  BrowserWindowInterface* home();
  // Makes `window` its home window; nullptr makes it the window its tab is
  // in now, which stays its home window when the tab moves.
  void SetHome(BrowserWindowInterface* window);
  // Makes `window` its home window and docks it there, by hand.
  void MoveHere(BrowserWindowInterface* window);
  // Runs `callback` each time a home window is set, and as placements come
  // and go.
  static base::CallbackListSubscription ObserveHomes(
      base::RepeatingClosure callback);

  // Floats it by hand, until DockByHand().
  void PopOut();
  // Docks every mini player that floats, by hand: brings this one's home
  // window forward, out of the Dock if minimized, and says again where each
  // goes.
  void DockByHand();
  // Brings `window` to the front, out of the Dock if minimized. A hidden
  // workspace window takes its partner's place.
  static void BringForward(BrowserWindowInterface* window);
  // True while it could dock in its home window's sidebar: with Dock chosen,
  // and the sidebar showing, not hidden and not full screen.
  bool CanDock();

  // The dock the mini player is in, or nullptr while it floats.
  void set_current(MiniPlayerDock* dock) { current_ = dock; }

  // BrowserCollectionObserver:
  void OnBrowserActivated(BrowserWindowInterface* browser) override;
  void OnBrowserDeactivated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // views::WidgetObserver, for the home window:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  MiniPlayerDock* Choose(bool just_opened = false);
  void Place(MiniPlayerDock* dock);
  // Follows the sidebar and the window of `home`.
  void Watch(BrowserWindowInterface* home, MiniPlayerDock* dock);

  const raw_ptr<views::Widget> floating_window_;
  const base::RepeatingCallback<content::WebContents*()> tab_;
  const PlaceCallback place_;
  base::WeakPtr<BrowserWindowInterface> home_;
  raw_ptr<MiniPlayerDock> current_ = nullptr;
  raw_ptr<BrowserWindowInterface> watched_ = nullptr;
  // Whether it has been placed yet, and whether it floats there.
  bool placed_ = false;
  bool floats_ = false;
  // Set by PopOut() until DockByHand().
  bool popped_out_ = false;
  PrefChangeRegistrar prefs_;
  base::CallbackListSubscription app_activity_;
  base::CallbackListSubscription collapse_;
  base::CallbackListSubscription cover_;
  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver>
      browsers_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver> window_{this};
  base::OneShotTimer settle_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PLACEMENT_H_
