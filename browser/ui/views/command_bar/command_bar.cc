// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_dialog_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_ui.h"

namespace uw {
namespace {

constexpr gfx::Size kSize(640, 360);
constexpr int kTopInset = 96;

}  // namespace

DEFINE_USER_DATA(CommandBar);

CommandBar::CommandBar(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {
  setting_.Init(window->GetProfile()->GetPrefs());
  setting_.Add(kCommandBarEnabledPref,
               base::BindRepeating(&CommandBar::OnSettingChanged,
                                   base::Unretained(this)));
  OnSettingChanged();
}

CommandBar::~CommandBar() {
  if (bubble_) {
    bubble_->ClearContentsWrapper();
  }
}

CommandBar* CommandBar::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

bool CommandBar::Open(BrowserWindowInterface* window) {
  CommandBar* bar = From(window);
  if (!bar || !bar->contents_) {
    return false;
  }
  bar->Show();
  return true;
}

bool CommandBar::IsShowing() const {
  return bubble_ && bubble_->GetWidget() && bubble_->GetWidget()->IsVisible();
}

content::WebContents* CommandBar::page() const {
  return contents_ ? contents_->web_contents() : nullptr;
}

void CommandBar::OnSettingChanged() {
  const bool on =
      window_->GetProfile()->GetPrefs()->GetBoolean(kCommandBarEnabledPref);
  if (on && !contents_) {
    contents_ = std::make_unique<WebUIContentsWrapperT<UwCommandBarUI>>(
        GURL(kCommandBarUrl), window_->GetProfile(), IDS_UW_COMMAND_BAR);
    webui::SetBrowserWindowInterface(contents_->web_contents(), window_);
  } else if (!on && contents_) {
    if (bubble_) {
      bubble_->GetWidget()->CloseNow();
    }
    contents_.reset();
  }
}

void CommandBar::Show() {
  if (IsShowing()) {
    return;
  }
  views::View* card =
      BrowserView::GetBrowserViewForBrowser(window_)->contents_container();
  const gfx::Rect area = card->GetBoundsInScreen();
  const gfx::Rect anchor(area.CenterPoint().x(), area.y() + kTopInset, 0, 0);
  auto view = std::make_unique<WebUIBubbleDialogView>(
      card, contents_->GetWeakPtr(), anchor, views::BubbleBorder::TOP_CENTER,
      /*autosize=*/false);
  view->web_view()->SetPreferredSize(kSize);
  bubble_ = view->GetWeakPtr();
  views::BubbleDialogDelegateView::CreateBubble(std::move(view));
  bubble_->ShowUI();
}

}  // namespace uw
