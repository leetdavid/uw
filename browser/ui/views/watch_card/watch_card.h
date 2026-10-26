// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WATCH_CARD_WATCH_CARD_H_
#define UW_BROWSER_UI_VIEWS_WATCH_CARD_WATCH_CARD_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "uw/browser/agent/scheduled_chats.h"

class BrowserWindowInterface;
class WebUIBubbleManager;

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

namespace views {
class Widget;
}

namespace uw {

// A window's eye in the address bar, just before the star, and the watch card
// under it, per docs/product.md, "Scheduled chats". The eye shows on web
// pages, outlined until the page is watched and filled after. A click opens
// the card, which watches the page or shows the scheduled chat that does.
class WatchCard : public TabStripModelObserver,
                  public ScheduledChats::Observer {
 public:
  DECLARE_USER_DATA(WatchCard);

  explicit WatchCard(BrowserWindowInterface* window);
  WatchCard(const WatchCard&) = delete;
  WatchCard& operator=(const WatchCard&) = delete;
  ~WatchCard() override;

  static WatchCard* From(BrowserWindowInterface* window);

  // Adds the eye's action to `window`. The run window has it too, though it
  // has no card, so that a tab keeps its page actions when it moves between
  // windows.
  static void AddAction(BrowserWindowInterface* window);

  // Opens the card under the eye for the selected tab's page. False when the
  // window has no card or the page cannot be watched.
  static bool Open(BrowserWindowInterface* window);
  // Whether `tab`'s page can be watched: a web page.
  static bool CanWatch(const tabs::TabInterface& tab);

  bool IsShowing() const;
  // Null until the card first opens.
  content::WebContents* page() const;
  views::Widget* GetWidgetForTesting() const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

 private:
  void Show(tabs::TabInterface& tab);
  // Shows `tab`'s eye, filled when its page is watched, or hides it.
  void UpdateEye(tabs::TabInterface& tab);
  void UpdateEyes();

  const raw_ptr<BrowserWindowInterface> window_;
  std::unique_ptr<WebUIBubbleManager> bubble_;
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer>
      chats_observation_{this};
  ui::ScopedUnownedUserData<WatchCard> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WATCH_CARD_WATCH_CARD_H_
