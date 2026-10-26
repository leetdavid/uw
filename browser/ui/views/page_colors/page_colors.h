// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_PAGE_COLORS_PAGE_COLORS_H_
#define UW_BROWSER_UI_VIEWS_PAGE_COLORS_PAGE_COLORS_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkColor.h"

class BrowserView;

namespace ui {
class Layer;
}

namespace uw {

// Gives one window's toolbar and bookmarks bar the page color of its active
// tab, and crossfades the top of the page card when that color changes.
class PageColors : public TabStripModelObserver,
                   public content::WebContentsObserver {
 public:
  static constexpr base::TimeDelta kFade = base::Milliseconds(200);

  explicit PageColors(BrowserView* view);
  PageColors(const PageColors&) = delete;
  PageColors& operator=(const PageColors&) = delete;
  ~PageColors() override;

 private:
  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void OnBackgroundColorChanged() override;

  // Takes the active tab's page color, if it changed.
  void Update();

  // Keeps what the top of the card shows now on a layer of its own, over the
  // newly colored one, and fades it out.
  void CrossFade();
  void EndFade(const ui::Layer* layer);

  const raw_ptr<BrowserView> view_;
  std::optional<SkColor> color_;
  std::unique_ptr<ui::Layer> fading_;
  base::WeakPtrFactory<PageColors> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_PAGE_COLORS_PAGE_COLORS_H_
