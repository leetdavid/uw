// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/pins/inactive_pin_overlay.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "uw/browser/pins/pin_pages.h"
#include "uw/browser/pins/pin_store.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"

namespace uw {
namespace {

constexpr float kBlur = 5;
constexpr float kSaturation = 0.7f;
// How far the picture reaches past the page on each side, so its blurred
// edges stay out of sight.
constexpr int kBleed = 12;
// The haze over the picture, lightening it in light mode and darkening it in
// dark mode.
constexpr SkColor kLightHaze = SkColorSetARGB(0x66, 0xCE, 0xCA, 0xDE);
constexpr SkColor kDarkHaze = SkColorSetARGB(0x85, 0x16, 0x15, 0x1F);
constexpr int kPillHeight = 34;
constexpr int kPillIcon = 16;
// Room around the pill for its shadow.
constexpr int kShadowRoom = 24;

// The picture of the page, drawn to cover the overlay, top first.
class PictureView : public views::View {
  METADATA_HEADER(PictureView, views::View)

 public:
  PictureView() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetLayerBlur(kBlur);
    layer()->SetLayerSaturation(kSaturation);
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetPicture(const gfx::ImageSkia& picture) {
    picture_ = picture;
    SchedulePaint();
  }

  const gfx::ImageSkia& picture() const { return picture_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawColor(GetColorProvider()->GetColor(kColorMenuCardBackground));
    if (picture_.isNull()) {
      return;
    }
    const gfx::Rect inside = GetLocalBounds();
    const float scale =
        std::max(static_cast<float>(inside.width()) / picture_.width(),
                 static_cast<float>(inside.height()) / picture_.height());
    const int width = picture_.width() * scale;
    const int height = picture_.height() * scale;
    canvas->DrawImageInt(picture_, 0, 0, picture_.width(), picture_.height(),
                         (inside.width() - width) / 2, 0, width, height,
                         /*filter=*/true);
  }

 private:
  gfx::ImageSkia picture_;
};

BEGIN_METADATA(PictureView)
END_METADATA

// "Open here", in a pill with a soft shadow.
class OpenHerePill : public views::View {
  METADATA_HEADER(OpenHerePill, views::View)

 public:
  OpenHerePill() {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
                         views::BoxLayout::Orientation::kHorizontal,
                         gfx::Insets::TLBR(kShadowRoom, kShadowRoom + 13,
                                           kShadowRoom, kShadowRoom + 16),
                         8))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kCenter);
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    label_ = AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_UW_OPEN_HERE)));
    const gfx::FontList base;
    label_->SetFontList(base.DeriveWithSizeDelta(13 - base.GetFontSize())
                            .DeriveWithWeight(gfx::Font::Weight::MEDIUM));
    label_->SetSubpixelRenderingEnabled(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::View::CalculatePreferredSize(available_size);
    size.set_height(kPillHeight + 2 * kShadowRoom);
    return size;
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    const ui::ColorProvider* colors = GetColorProvider();
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        kLogInIcon, colors->GetColor(kColorMenuCardAccent), kPillIcon));
    label_->SetEnabledColor(colors->GetColor(kColorMenuCardText));
    label_->SetBackgroundColor(colors->GetColor(kColorMenuCardBackground));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    gfx::RectF pill(GetLocalBounds());
    pill.Inset(kShadowRoom);
    const float radius = pill.height() / 2;
    cc::PaintFlags shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(colors->GetColor(kColorMenuCardBackground));
    shadow.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(
        gfx::Vector2d(0, 8), 24, SkColorSetARGB(0x2E, 0x1E, 0x1B, 0x2E))}));
    canvas->DrawRoundRect(pill, radius, shadow);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(colors->GetColor(kColorMenuCardBackground));
    canvas->DrawRoundRect(pill, radius, fill);
    cc::PaintFlags edge;
    edge.setAntiAlias(true);
    edge.setStyle(cc::PaintFlags::kStroke_Style);
    edge.setStrokeWidth(1);
    edge.setColor(colors->GetColor(kColorMenuCardLine));
    pill.Inset(0.5f);
    canvas->DrawRoundRect(pill, radius - 0.5f, edge);
  }

 private:
  raw_ptr<views::ImageView> icon_;
  raw_ptr<views::Label> label_;
};

BEGIN_METADATA(OpenHerePill)
END_METADATA

}  // namespace

// The picture, the haze over it, and the pill.
class HazeView : public views::View {
  METADATA_HEADER(HazeView, views::View)

 public:
  explicit HazeView(base::RepeatingClosure clicked)
      : clicked_(std::move(clicked)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
    layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(0, 0, kPageCardRadius, kPageCardRadius));
    layer()->SetIsFastRoundedCorner(true);
    picture_ = AddChildView(std::make_unique<PictureView>());
    // On layers of their own, so they stay over the picture's.
    haze_ = AddChildView(std::make_unique<views::View>());
    haze_->SetPaintToLayer();
    haze_->layer()->SetFillsBoundsOpaquely(false);
    haze_->SetCanProcessEventsWithinSubtree(false);
    pill_ = AddChildView(std::make_unique<OpenHerePill>());
    pill_->SetPaintToLayer();
    pill_->layer()->SetFillsBoundsOpaquely(false);
  }

  void SetPicture(const gfx::ImageSkia& picture) {
    picture_->SetPicture(picture);
  }

  views::View* pill() { return pill_; }
  const gfx::ImageSkia& picture() const { return picture_->picture(); }

  // views::View:
  void Layout(PassKey) override {
    gfx::Rect bleed = GetLocalBounds();
    bleed.Outset(kBleed);
    picture_->SetBoundsRect(bleed);
    haze_->SetBoundsRect(GetLocalBounds());
    const gfx::Size pill = pill_->GetPreferredSize();
    pill_->SetBounds((width() - pill.width()) / 2,
                     (height() - pill.height()) / 2, pill.width(),
                     pill.height());
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    const bool dark = color_utils::IsDark(
        GetColorProvider()->GetColor(kColorMenuCardBackground));
    haze_->SetBackground(
        views::CreateSolidBackground(dark ? kDarkHaze : kLightHaze));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    return event.IsOnlyLeftMouseButton();
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton() && HitTestPoint(event.location())) {
      clicked_.Run();
    }
  }

 private:
  base::RepeatingClosure clicked_;
  raw_ptr<PictureView> picture_;
  raw_ptr<views::View> haze_;
  raw_ptr<views::View> pill_;
};

BEGIN_METADATA(HazeView)
END_METADATA

DEFINE_USER_DATA(InactivePinOverlay);

InactivePinOverlay::InactivePinOverlay(BrowserView* browser_view)
    : browser_view_(browser_view),
      user_data_(browser_view->browser()->GetUnownedUserDataHost(), *this) {
  browser_view_->browser()->tab_strip_model()->AddObserver(this);
  if (PinPages* pages = PinPages::For(browser_view_->browser()->GetProfile())) {
    pictures_ = pages->ObservePictures(base::BindRepeating(
        &InactivePinOverlay::Update, base::Unretained(this)));
  }
}

InactivePinOverlay::~InactivePinOverlay() {
  browser_view_->browser()->tab_strip_model()->RemoveObserver(this);
  contents_.Reset();
  if (HazeView* view = haze(); view && view->parent()) {
    view->parent()->RemoveChildViewT(view);
  }
}

// static
InactivePinOverlay* InactivePinOverlay::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

views::View* InactivePinOverlay::GetShownView() {
  HazeView* view = haze();
  return view && view->GetVisible() ? view : nullptr;
}

HazeView* InactivePinOverlay::haze() {
  return static_cast<HazeView*>(haze_.view());
}

void InactivePinOverlay::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  Update();
}

void InactivePinOverlay::OnViewBoundsChanged(views::View* observed_view) {
  if (HazeView* view = haze()) {
    view->SetBoundsRect(observed_view->bounds());
  }
}

void InactivePinOverlay::OnViewIsDeleting(views::View* observed_view) {
  contents_.Reset();
}

void InactivePinOverlay::Update() {
  tabs::TabInterface* tab =
      browser_view_->browser()->tab_strip_model()->GetActiveTab();
  const PinStore::Pin* pin =
      PinSync::IsPlaceholder(tab) ? PinSync::PinFor(tab) : nullptr;
  ContentsContainerView* container =
      pin ? browser_view_->GetActiveContentsContainerView() : nullptr;
  HazeView* view = haze();
  if (!container) {
    if (view) {
      view->SetVisible(false);
    }
    return;
  }
  if (!view || view->parent() != container) {
    contents_.Reset();
    if (view) {
      view->parent()->RemoveChildViewT(view);
    }
    view =
        container->AddChildView(std::make_unique<HazeView>(base::BindRepeating(
            &InactivePinOverlay::BringPage, base::Unretained(this))));
    haze_.SetView(view);
    contents_.Observe(container->contents_view());
  }
  view->SetBoundsRect(container->contents_view()->bounds());
  view->SetPicture(PinPages::For(browser_view_->browser()->GetProfile())
                       ->PictureOf(pin->id));
  view->SetVisible(true);
}

void InactivePinOverlay::BringPage() {
  PinSync::BringLivePage(
      browser_view_->browser()->tab_strip_model()->GetActiveTab());
}

}  // namespace uw
