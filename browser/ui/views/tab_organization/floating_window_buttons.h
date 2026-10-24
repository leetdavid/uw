// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_WINDOW_BUTTONS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_WINDOW_BUTTONS_H_

#include "base/callback_list.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace uw {

// The close, minimize, and zoom buttons on a hidden sidebar's floating card.
// uw draws them as macOS draws its own, which a hidden sidebar takes away, so
// they slide in with the card. While the pointer is on them all three show
// their glyphs, and in a window that is not in front they are faint.
class FloatingWindowButtons : public views::View {
  METADATA_HEADER(FloatingWindowButtons, views::View)

 public:
  FloatingWindowButtons();
  FloatingWindowButtons(const FloatingWindowButtons&) = delete;
  FloatingWindowButtons& operator=(const FloatingWindowButtons&) = delete;
  ~FloatingWindowButtons() override;

  bool hovered() const { return hovered_; }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

 private:
  class Light;

  void SetHovered(bool hovered);

  bool hovered_ = false;
  base::CallbackListSubscription paint_as_active_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_WINDOW_BUTTONS_H_
