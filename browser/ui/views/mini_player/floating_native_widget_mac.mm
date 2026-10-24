// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/floating_native_widget_mac.h"

#import <Cocoa/Cocoa.h>

#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"

namespace uw {

FloatingNativeWidgetMac::FloatingNativeWidgetMac(views::Widget* widget)
    : views::NativeWidgetMac(widget) {}

FloatingNativeWidgetMac::~FloatingNativeWidgetMac() = default;

void FloatingNativeWidgetMac::PopulateCreateWindowParams(
    const views::Widget::InitParams& widget_params,
    remote_cocoa::mojom::CreateWindowParams* params) {
  params->window_class = remote_cocoa::mojom::WindowClass::kFrameless;
  params->style_mask = NSWindowStyleMaskFullSizeContentView;
}

}  // namespace uw
