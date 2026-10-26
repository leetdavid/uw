// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_STACK_ROW_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_STACK_ROW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"

namespace views {
class ImageButton;
}

namespace uw {

// A paused video of the stack, fanned out above the docked mini player: its
// still, its title, and where it stopped. A click plays it again; its × drops
// it from the stack.
class StackRow : public views::Button {
  METADATA_HEADER(StackRow, views::Button)

 public:
  static constexpr int kHeight = 52;

  StackRow(const MiniPlayerStack::Entry& entry,
           PressedCallback play,
           PressedCallback drop);
  StackRow(const StackRow&) = delete;
  StackRow& operator=(const StackRow&) = delete;
  ~StackRow() override;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void StateChanged(ButtonState old_state) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  SkColor TextColor() const;
  bool OnLightSidebar() const;

  const MiniPlayerStack::Entry entry_;
  raw_ptr<views::ImageButton> drop_ = nullptr;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_STACK_ROW_H_
