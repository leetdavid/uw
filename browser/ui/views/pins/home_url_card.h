// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_PINS_HOME_URL_CARD_H_
#define UW_BROWSER_UI_VIEWS_PINS_HOME_URL_CARD_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class GURL;

namespace tabs {
class TabInterface;
}

namespace views {
class Textfield;
class Widget;
}  // namespace views

namespace uw {

// The card under a pin's tile that edits its home URL. Save keeps the address
// and loads it in the pin.
class HomeUrlCard : public views::View {
  METADATA_HEADER(HomeUrlCard, views::View)

 public:
  // Opens the card of `tab` under `tile`, closing any other. Null when `tab`
  // is no pin or its tile is gone.
  static views::Widget* Show(const tabs::TabInterface* tab, views::View* tile);

  explicit HomeUrlCard(const GURL& home);
  HomeUrlCard(const HomeUrlCard&) = delete;
  HomeUrlCard& operator=(const HomeUrlCard&) = delete;
  ~HomeUrlCard() override;

  views::Textfield* field() { return field_; }

 private:
  raw_ptr<views::Textfield> field_ = nullptr;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_PINS_HOME_URL_CARD_H_
