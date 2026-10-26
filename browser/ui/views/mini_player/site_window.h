// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_SITE_WINDOW_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_SITE_WINDOW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;

namespace content {
class DocumentPictureInPictureWindowController;
}

namespace uw {

class MiniPlayerDock;
class MiniPlayerPlacement;

// A site's own picture-in-picture window, such as a Meet call, per "Sites' own
// windows" in docs/plans/mini-player.md. Chromium shows the site's page in a
// small browser window of its own. While the site asked for a small window
// and the mini player docks, uw moves the live page out of that window into
// the sidebar, under any mini player, and back into its window when it
// floats. Beside a video it is a live window, which keeps running, unless the
// user turned that off for its site: then it pauses and waits in the stack
// like a video.
class SiteWindow : public content::WebContentsDelegate,
                   public content::WebContentsObserver,
                   public views::WidgetObserver,
                   public PictureInPictureWindowManager::Observer {
 public:
  // The biggest window, each way, a site may ask for and still dock.
  static constexpr int kMaxDockedSide = 480;

  // Starts following sites' windows as they open; called with each sidebar.
  static void Watch();
  // The site's window open now, or nullptr.
  static SiteWindow* Current();

  SiteWindow(const SiteWindow&) = delete;
  SiteWindow& operator=(const SiteWindow&) = delete;
  ~SiteWindow() override;

  // The tab that opened it.
  content::WebContents* opener() { return opener_; }
  // The host of that tab, whose choice of Live it follows.
  const std::string& site() const { return site_; }
  // Whether it keeps running beside a video.
  bool live() const;
  // True while it waits in the stack, paused, under a video.
  bool waiting() const { return waiting_; }
  // The site's page in it.
  content::WebContents* page() { return page_; }
  // Its own browser window, hidden while the page is docked.
  BrowserWindowInterface* own_window() { return window_; }
  // The size the site asked for.
  gfx::Size asked_size() const { return asked_; }
  // The dock the page is in, or nullptr while it floats.
  MiniPlayerDock* dock() { return dock_; }
  // False when the site asked to keep the user from going back to its tab.
  bool ShowsBackToTab() const;
  // Its own window's widget.
  views::Widget* own_widget();

  // Closes it, for its ×.
  void Close();
  // Closes it and goes to the tab that opened it.
  void BackToTab();
  // Floats it by hand, for its pop out, until docked by hand.
  void PopOut();

  // The dock button in the bar of a site's floating window, which uw shows
  // while that window could dock.
  static std::unique_ptr<views::View> MakeBarDockButton();

  // content::WebContentsDelegate, while the page is docked:
  void CloseContents(content::WebContents* source) override;
  void SetContentsBounds(content::WebContents* source,
                         const gfx::Rect& bounds) override;
  void ActivateContents(content::WebContents* contents) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // content::WebContentsObserver, on the page:
  void WebContentsDestroyed() override;

  // views::WidgetObserver, on its own window:
  void OnWidgetDestroying(views::Widget* widget) override;

  // PictureInPictureWindowManager::Observer:
  void OnEnterPictureInPicture() override;
  void OnExitPictureInPicture() override;

 private:
  friend class SiteWindowWatcher;

  SiteWindow(content::WebContents* opener,
             content::WebContents* page,
             BrowserWindowInterface* window);

  // Docks the page in `dock`, or floats it in its own window when nullptr.
  void Place(MiniPlayerDock* dock);
  void Float();
  // Takes the page out of the dock it is in.
  void Undock();
  // Shows the bar's dock button while it floats and could dock.
  void UpdateBarDockButton(bool floats);
  // True while a video plays above it in Chromium's eyes.
  bool BesideAVideo();
  // Pauses it into the stack, or closes it outside the sidebar, unless it is
  // live.
  void MeetVideo();
  void Wait();
  void Resume();
  void OnLiveChanged();
  // True while it must float: too big, grown too big, or asking the user.
  bool MustFloat() const;
  bool AsksTheUser() const;
  // The delegate of the window the page is docked in, for its keys.
  content::WebContentsDelegate* DockDelegate();
  content::DocumentPictureInPictureWindowController* controller();

  const raw_ptr<content::WebContents> opener_;
  const raw_ptr<content::WebContents> page_;
  const raw_ptr<BrowserWindowInterface> window_;
  const std::string site_;
  bool waiting_ = false;
  PrefChangeRegistrar prefs_;
  base::ScopedObservation<PictureInPictureWindowManager,
                          PictureInPictureWindowManager::Observer>
      picture_in_picture_{this};
  gfx::Size asked_;
  // Set once the site grew it past the limit; it floats until it closes.
  bool grew_too_big_ = false;
  raw_ptr<MiniPlayerDock> dock_ = nullptr;
  // The page while it is out of its own window, which holds a blank page.
  std::unique_ptr<content::WebContents> held_page_;
  std::unique_ptr<MiniPlayerPlacement> placement_;
  // Looks again while Chromium asks the user something in the window.
  base::RepeatingTimer prompt_check_;
  // Set once its own window closes, as when uw quits.
  bool own_window_gone_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      own_window_observation_{this};
  base::WeakPtrFactory<SiteWindow> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_SITE_WINDOW_H_
