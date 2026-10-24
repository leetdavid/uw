// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/tint_style.h"

#include "base/functional/bind.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/glass/glass.h"

namespace uw {

void StyleAsPill(views::MdTextButton* button) {
  // A text button draws no edge, which would show over a see-through fill.
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetBgColorIdOverride(kColorNewTabButtonCRBackgroundFrameActive);
  button->SetEnabledTextColors(kColorTabForegroundInactiveFrameActive);
}

void StyleOnTint(views::Label* label) {
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(kColorTabForegroundInactiveFrameActive);
  label->SetBackgroundColor(SK_ColorTRANSPARENT);
}

GlassOf GlassOfView(const views::View* view) {
  return base::BindRepeating(
      [](const views::View* view) {
        const views::Widget* widget = view->GetWidget();
        BrowserView* browser_view =
            widget ? BrowserView::GetBrowserViewForNativeWindow(
                         widget->GetNativeWindow())
                   : nullptr;
        return browser_view ? ElementGlassOfWindow(browser_view->browser()) : 0;
      },
      base::Unretained(view));
}

}  // namespace uw
