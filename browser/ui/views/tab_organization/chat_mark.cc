// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/chat_mark.h"

#include <memory>
#include <string>

#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"

namespace uw {
namespace {

constexpr int kSize = 12;
constexpr float kDotRadius = 3.5f;
constexpr float kPauseBar = 2.0f;
constexpr float kPauseGap = 2.5f;
constexpr float kPauseHeight = 8.0f;
constexpr SkAlpha kPauseAlpha = 0xB3;
// The chat page's amber, which marks what waits for the user.
constexpr SkColor kWaiting = SkColorSetRGB(0xE8, 0x71, 0x0A);

// The id of the chat `contents` shows, or empty for any other page.
std::string ChatIdOf(content::WebContents* contents) {
  const GURL& url = contents->GetVisibleURL();
  if (!url.SchemeIs(content::kChromeUIScheme) || url.host() != kChatHost) {
    return std::string();
  }
  return std::string(url.path().substr(1));
}

}  // namespace

ChatMark::ChatMark(tabs::TabHandle tab) : tab_(tab) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  spinner_ = AddChildView(std::make_unique<views::Throbber>(kSize));
  spinner_->SetVisible(false);
  SetVisible(false);
}

ChatMark::~ChatMark() = default;

void ChatMark::AddedToWidget() {
  View::AddedToWidget();
  tabs::TabInterface* tab = tab_.Get();
  ChatIndex* index =
      tab ? ChatIndex::For(tab->GetBrowserWindowInterface()->GetProfile())
          : nullptr;
  if (index) {
    observation_.Observe(index);
  }
  Refresh();
}

void ChatMark::RemovedFromWidget() {
  observation_.Reset();
  View::RemovedFromWidget();
}

gfx::Size ChatMark::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kSize, kSize);
}

void ChatMark::OnPaint(gfx::Canvas* canvas) {
  View::OnPaint(canvas);
  if (!state_ || *state_ == ChatIndex::State::kRunning) {
    return;
  }
  const gfx::RectF box(GetContentsBounds());
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  if (*state_ == ChatIndex::State::kStopped) {
    flags.setColor(SkColorSetA(
        GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive),
        kPauseAlpha));
    const float left = box.CenterPoint().x() - kPauseGap / 2 - kPauseBar;
    const float top = box.CenterPoint().y() - kPauseHeight / 2;
    canvas->DrawRect(gfx::RectF(left, top, kPauseBar, kPauseHeight), flags);
    canvas->DrawRect(
        gfx::RectF(left + kPauseBar + kPauseGap, top, kPauseBar, kPauseHeight),
        flags);
    return;
  }
  flags.setColor(*state_ == ChatIndex::State::kWaiting
                     ? kWaiting
                     : GetColorProvider()->GetColor(ui::kColorAccent));
  canvas->DrawCircle(box.CenterPoint(), kDotRadius, flags);
}

void ChatMark::OnChatsChanged() {
  Refresh();
}

void ChatMark::Refresh() {
  std::optional<ChatIndex::State> state;
  tabs::TabInterface* tab = tab_.Get();
  const std::string id = tab ? ChatIdOf(tab->GetContents()) : std::string();
  ChatIndex* index = observation_.GetSource();
  if (index && !id.empty()) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      if (entry.id == id && entry.state != ChatIndex::State::kDone) {
        state = entry.state;
      }
    }
  }
  if (state == state_) {
    return;
  }
  state_ = state;
  const bool running = state_ == ChatIndex::State::kRunning;
  spinner_->SetVisible(running);
  if (running) {
    spinner_->Start();
  } else {
    spinner_->Stop();
  }
  const char16_t* name =
      !state_                                 ? u""
      : running                               ? u"The agent is working"
      : *state_ == ChatIndex::State::kWaiting ? u"Waits for you"
      : *state_ == ChatIndex::State::kStopped ? u"Stopped before it finished"
                                              : u"New answer";
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  SchedulePaint();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

BEGIN_METADATA(ChatMark)
END_METADATA

}  // namespace uw
