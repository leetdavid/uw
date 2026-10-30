// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_HINT_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_HINT_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Button;
}

namespace uw {

// What a held Option would change about a click or drag on a tab row, shown
// before the click: the rows an Option-click on a tab's close button closes,
// tinted, or the rows an Option-drag of a parent leaves behind, faded. The
// row under the pointer decides, and every row of its branch shows it. See
// docs/plans/keyboard-and-mouse.md#key-hints.
//
// It covers its row and takes no events. It also keeps the close button's
// tooltip, which names Option on a parent's row, and with Option held on the
// rest of a parent's row, it is the row's tooltip.
class KeyHint : public views::View {
  METADATA_HEADER(KeyHint, views::View)

 public:
  enum class Shown { kNone, kCloses, kStays };

  // `close` is the row's close button, and `update_colors` has the row
  // recolor it.
  KeyHint(tabs::TabHandle tab,
          views::Button* close,
          base::RepeatingClosure update_colors);
  KeyHint(const KeyHint&) = delete;
  KeyHint& operator=(const KeyHint&) = delete;
  ~KeyHint() override;

  Shown shown() const { return shown_; }
  // The pointer is on the row, as Chromium's tab view keeps it.
  void SetRowHovered(bool hovered);
  // The color of the close button's ×: the close color while an
  // Option-click on it would close the branch.
  SkColor CloseColor(SkColor color) const;

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnPaint(gfx::Canvas* canvas) override;
  View* GetTooltipHandlerForPoint(const gfx::Point& point) override;

 private:
  void OnCloseState();
  // Draws the hint the pointer and the keys call for, and the tooltip.
  void Update();
  void SetFaded(bool faded);

  const tabs::TabHandle tab_;
  const raw_ptr<views::Button> close_;
  const base::RepeatingClosure update_colors_;
  Shown shown_ = Shown::kNone;
  bool source_ = false;
  base::CallbackListSubscription keys_;
  base::CallbackListSubscription hints_;
  base::CallbackListSubscription tree_;
  base::CallbackListSubscription drags_;
  base::CallbackListSubscription close_state_;
};

// The close color, for the hint's tint and the ×: red on a light sidebar,
// and a lighter red on a dark one.
SkColor CloseHintColor(const views::View* view);

// Hooks for Chromium's vertical tab row, in tab-tree.patch.
void SetRowHovered(views::View* row, bool hovered);
SkColor CloseButtonColor(const views::View* row, SkColor color);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEY_HINT_H_
