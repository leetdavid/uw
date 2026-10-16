// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/theme_picker/theme_picker.h"

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/grit/generated_resources.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker_ui.h"

namespace uw {
namespace {

// Beside the sidebar, which ends where the page card starts, near the top.
constexpr int kGap = 10;
constexpr int kTopInset = 56;

}  // namespace

DEFINE_USER_DATA(ThemePicker);

ThemePicker::ThemePicker(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {}

ThemePicker::~ThemePicker() = default;

ThemePicker* ThemePicker::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

bool ThemePicker::Open(BrowserWindowInterface* window) {
  ThemePicker* picker = From(window);
  if (!picker) {
    return false;
  }
  picker->Show();
  return true;
}

bool ThemePicker::IsShowing() const {
  return bubble_ && bubble_->GetBubbleWidget() &&
         bubble_->GetBubbleWidget()->IsVisible();
}

content::WebContents* ThemePicker::page() const {
  return bubble_ && bubble_->GetContentsWrapper()
             ? bubble_->GetContentsWrapper()->web_contents()
             : nullptr;
}

views::Widget* ThemePicker::GetWidgetForTesting() const {
  return bubble_ ? bubble_->GetBubbleWidget() : nullptr;
}

void ThemePicker::Show() {
  if (!bubble_) {
    bubble_ = WebUIBubbleManager::Create<UwThemePickerUI>(
        window_, GURL(kThemePickerUrl), IDS_UW_THEME_PICKER);
  }
  if (bubble_->GetBubbleWidget()) {
    return;
  }
  const gfx::Rect card = BrowserView::GetBrowserViewForBrowser(window_)
                             ->contents_container()
                             ->GetBoundsInScreen();
  bubble_->ShowBubble(gfx::Rect(card.x() + kGap, card.y() + kTopInset, 0, 0),
                      views::BubbleBorder::TOP_LEFT);
}

}  // namespace uw
