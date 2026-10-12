// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ACTIVITY_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ACTIVITY_CARD_H_

#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/flex_layout_view.h"
#include "uw/browser/tab_organization/automatic_organizer.h"

namespace views {
class ImageView;
class Label;
class MdTextButton;
class ProgressBar;
class Throbber;
}  // namespace views

namespace uw {

// A card at the foot of the tab sidebar that shows the organizer's work on
// one tab. While a model is asked it names the model and the tab, with a
// spinner and a moving bar. After the answer it says what the model chose,
// how confident it was against what a move needs, and how long it took.
// After a move it carries the Undo button.
//
// Every line is one line tall and shortens with an ellipsis, so the card's
// height never depends on its width and nothing in it is cut off.
class ActivityCard : public views::FlexLayoutView {
  METADATA_HEADER(ActivityCard, views::FlexLayoutView)

 public:
  explicit ActivityCard(base::RepeatingClosure undo);
  ActivityCard(const ActivityCard&) = delete;
  ActivityCard& operator=(const ActivityCard&) = delete;
  ~ActivityCard() override;

  void Show(const AutomaticOrganizer::Event& event);
  // Puts the card away and stops its spinner and its moving bar.
  void Hide();

  std::u16string_view headline() const;
  std::u16string_view page() const;
  // Empty unless the model gave a confidence.
  std::u16string_view confidence() const;
  // The model and how long it took. Empty while the request is out.
  std::u16string_view source() const;
  // True while a model is being asked.
  bool waiting() const;
  bool offers_undo() const;

 private:
  raw_ptr<views::Throbber> throbber_ = nullptr;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> headline_ = nullptr;
  raw_ptr<views::Label> page_ = nullptr;
  raw_ptr<views::ProgressBar> meter_ = nullptr;
  raw_ptr<views::Label> confidence_ = nullptr;
  raw_ptr<views::Label> source_ = nullptr;
  raw_ptr<views::MdTextButton> undo_ = nullptr;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ACTIVITY_CARD_H_
