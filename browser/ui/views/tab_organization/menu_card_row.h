// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MENU_CARD_ROW_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MENU_CARD_ROW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"

namespace gfx {
struct VectorIcon;
}

namespace ui {
class Accelerator;
class KeyEvent;
}  // namespace ui

namespace views {
class BubbleDialogDelegate;
class ImageView;
class Label;
}  // namespace views

namespace uw {

// A row of one of uw's menu cards: an icon, a label, and whatever ends the
// row, such as its key. The hover fill also marks the row the arrow keys
// reached.
class MenuCardRow : public views::Button {
  METADATA_HEADER(MenuCardRow, views::Button)

 public:
  MenuCardRow(PressedCallback callback,
              const gfx::VectorIcon& icon,
              const std::u16string& text);
  MenuCardRow(const MenuCardRow&) = delete;
  MenuCardRow& operator=(const MenuCardRow&) = delete;
  ~MenuCardRow() override;

  views::Label* label() { return label_; }
  // Ends the label's line with `view`.
  void SetEnd(std::unique_ptr<views::View> view);
  // A second line under the label, in the muted color.
  void SetDetail(std::unique_ptr<views::View> detail);

  // views::Button:
  void StateChanged(ButtonState old_state) override;
  void OnFocus() override;
  void OnBlur() override;
  KeyClickAction GetKeyClickActionForEvent(const ui::KeyEvent& event) override;

 private:
  void Update();

  const raw_ref<const gfx::VectorIcon> icon_;
  raw_ptr<views::ImageView> image_ = nullptr;
  raw_ptr<views::View> line_ = nullptr;
  raw_ptr<views::View> column_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
};

// A small pill in a menu card, such as a pause length.
class MenuCardChip : public views::LabelButton {
  METADATA_HEADER(MenuCardChip, views::LabelButton)

 public:
  MenuCardChip(PressedCallback callback, const std::u16string& text);
  ~MenuCardChip() override;

  // views::LabelButton:
  void StateChanged(ButtonState old_state) override;
  void OnFocus() override;
  void OnBlur() override;
  KeyClickAction GetKeyClickActionForEvent(const ui::KeyEvent& event) override;

 private:
  void Update();
};

// Lets a row, chip, or dot of a menu card take focus, so the arrow keys
// reach it with Full Keyboard Access off, as they reach a macOS menu's items.
// The hover fill marks it instead of a focus ring.
void SetUpMenuCardFocus(views::Button* button);
// What a key does to a focused row, chip, or dot of a menu card. Return runs
// it, as in a macOS menu, though Chromium's buttons on macOS take only Space.
views::Button::KeyClickAction MenuCardKeyClickAction(const ui::KeyEvent& event);

// The same icon and label as a row, for a row that takes no click.
std::unique_ptr<views::View> MakeMenuCardHeader(const gfx::VectorIcon& icon,
                                                const std::u16string& text);
std::unique_ptr<views::View> MakeMenuCardSeparator();
// A key at a row's end, such as "⌥⌘Z".
std::unique_ptr<views::Label> MakeMenuCardKey(
    const ui::Accelerator& accelerator);
// Text in the muted color, smaller than a row's label.
std::unique_ptr<views::Label> MakeMenuCardDetail(const std::u16string& text);

// Gives the bubble of a menu card the look of a uw menu card. The arrow keys
// move between its rows.
void SetUpMenuCard(views::BubbleDialogDelegate* bubble);
// Lays out the rows of a menu card's contents top to bottom.
void LayOutMenuCard(views::View* contents);
// Frosts the card of `bubble` once it shows, while its window's glass is on.
void FrostOverGlass(views::BubbleDialogDelegate* bubble);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MENU_CARD_ROW_H_
