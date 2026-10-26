// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/live_pill.h"

#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

namespace uw {
namespace {

constexpr int kHeight = 22;
constexpr int kDot = 7;
constexpr int kDotLeft = 7;
constexpr int kGap = 5;
constexpr int kRight = 9;
// iOS's green, as the design draws it.
constexpr SkColor kLiveGreen = SkColorSetRGB(0x34, 0xC7, 0x59);

gfx::FontList Font() {
  const gfx::FontList& base = views::Label::GetDefaultFontList();
  return base.DeriveWithSizeDelta(11 - base.GetFontSize())
      .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
}

}  // namespace

// static
std::unique_ptr<LivePill> LivePill::ForOpener(content::WebContents* opener,
                                              Look look) {
  return std::make_unique<LivePill>(
      opener ? std::string(opener->GetLastCommittedURL().host())
             : std::string(),
      look);
}

LivePill::LivePill(std::string site, Look look)
    : views::Button(
          base::BindRepeating(&LivePill::Toggle, base::Unretained(this))),
      site_(std::move(site)),
      look_(look) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_UW_MINI_PLAYER_LIVE));
  if (PrefService* local_state = g_browser_process->local_state()) {
    prefs_.Init(local_state);
    prefs_.Add(
        kMiniPlayerSiteWindowsPref,
        base::BindRepeating(&LivePill::OnChanged, base::Unretained(this)));
  }
  OnChanged();
}

LivePill::~LivePill() = default;

bool LivePill::live() const {
  return IsSiteLive(g_browser_process->local_state(), site_);
}

gfx::Size LivePill::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(
      kDotLeft + kDot + kGap +
          gfx::GetStringWidth(
              l10n_util::GetStringUTF16(IDS_UW_MINI_PLAYER_LIVE), Font()) +
          kRight,
      kHeight);
}

void LivePill::PaintButtonContents(gfx::Canvas* canvas) {
  const SkColor text =
      look_ == Look::kOverPage
          ? SK_ColorWHITE
          : GetColorProvider()->GetColor(kColorPipWindowForeground);
  const bool hovered =
      GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(look_ == Look::kOverPage
                     ? SkColorSetA(SK_ColorBLACK, hovered ? 0x80 : 0x66)
                     : SkColorSetA(text, hovered ? 0x29 : 0x14));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), height() / 2.0f, flags);

  // The dot: green with a halo while live, a hollow ring while not.
  const gfx::PointF center(kDotLeft + kDot / 2.0f, height() / 2.0f);
  if (live()) {
    flags.setColor(SkColorSetA(kLiveGreen, 0x52));
    canvas->DrawCircle(center, kDot / 2.0f + 2, flags);
    flags.setColor(kLiveGreen);
    canvas->DrawCircle(center, kDot / 2.0f, flags);
  } else {
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.5f);
    flags.setColor(SkColorSetA(text, 0xC7));
    canvas->DrawCircle(center, kDot / 2.0f - 0.75f, flags);
  }
  const int left = kDotLeft + kDot + kGap;
  canvas->DrawStringRectWithFlags(
      l10n_util::GetStringUTF16(IDS_UW_MINI_PLAYER_LIVE), Font(),
      live() ? text : SkColorSetA(text, 0xC7),
      gfx::Rect(left, 0, width() - left, height()),
      gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

void LivePill::Toggle() {
  SetSiteLive(g_browser_process->local_state(), site_, !live());
}

void LivePill::OnChanged() {
  const bool on = live();
  GetViewAccessibility().SetCheckedState(on ? ax::mojom::CheckedState::kTrue
                                            : ax::mojom::CheckedState::kFalse);
  SetTooltipText(
      l10n_util::GetStringUTF16(on ? IDS_UW_MINI_PLAYER_LIVE_ON_TOOLTIP
                                   : IDS_UW_MINI_PLAYER_LIVE_OFF_TOOLTIP));
  SchedulePaint();
}

BEGIN_METADATA(LivePill)
END_METADATA

}  // namespace uw
