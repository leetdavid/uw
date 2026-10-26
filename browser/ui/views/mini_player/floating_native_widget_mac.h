// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_FLOATING_NATIVE_WIDGET_MAC_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_FLOATING_NATIVE_WIDGET_MAC_H_

#include "ui/views/widget/native_widget_mac.h"

namespace uw {

// The floating mini player's window on macOS: frameless, so its own rounded
// corners and shadow are all that show, as Chromium's picture-in-picture
// window is.
class FloatingNativeWidgetMac : public views::NativeWidgetMac {
 public:
  explicit FloatingNativeWidgetMac(views::Widget* widget);
  FloatingNativeWidgetMac(const FloatingNativeWidgetMac&) = delete;
  FloatingNativeWidgetMac& operator=(const FloatingNativeWidgetMac&) = delete;
  ~FloatingNativeWidgetMac() override;

 protected:
  // views::NativeWidgetMac:
  void PopulateCreateWindowParams(
      const views::Widget::InitParams& widget_params,
      remote_cocoa::mojom::CreateWindowParams* params) override;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_FLOATING_NATIVE_WIDGET_MAC_H_
