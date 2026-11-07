// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/workspace_swipe.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/custom_floating_corner.h"
#include "chrome/browser/ui/views/frame/main_background_region_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/look/tint_painter.h"
#include "uw/browser/ui/views/workspaces/workspace_switcher.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// How far the fingers move before the swipe knows its axis, as Chromium's
// own swipe on the vertical tab strip does.
constexpr float kAxisLock = 15;
// Past the first or last workspace, the sidebar gives no more than this.
constexpr float kStretch = 40;
// A flick this fast, in pixels per millisecond, lands from this far.
constexpr float kFlickSpeed = 0.3f;
constexpr float kFlickDistance = 30;
// How much of the swipe's end the flick's speed is taken over.
constexpr base::TimeDelta kSpeedWindow = base::Milliseconds(80);
constexpr base::TimeDelta kSnapBack = base::Milliseconds(200);
// A landing from the start takes this long, and less from further along.
constexpr base::TimeDelta kLand = base::Milliseconds(150);
constexpr base::TimeDelta kShortestLand = base::Milliseconds(60);
constexpr base::TimeDelta kKeySlide = base::Milliseconds(175);
constexpr base::TimeDelta kFade = base::Milliseconds(150);
// How long the page of the window left stays over a page still loading.
constexpr base::TimeDelta kCoverWait = base::Seconds(1);
// The other workspace's window opens once its profile loads.
constexpr base::TimeDelta kGiveUp = base::Seconds(5);
// Getting the other workspaces ready starts this long after the window shows.
// It waits no longer than this for the window's own page to draw, and gives a
// workspace no longer than this in all.
constexpr base::TimeDelta kReadyAfterShowing = base::Milliseconds(300);
constexpr base::TimeDelta kReadyPageWait = base::Seconds(3);
constexpr base::TimeDelta kReadyGiveUp = base::Seconds(8);
// A sidebar that changed behind its partner is pictured again once its
// changes stop for this long.
constexpr base::TimeDelta kStaleAfter = base::Seconds(1);

bool g_ready_in_tests = false;

// Copies `layer` when it next draws, and gives `done` the part `area` covers,
// in the layer's coordinates. Null when the copy fails.
void Copy(ui::Layer* layer,
          const gfx::Rect& area,
          base::OnceCallback<void(gfx::ImageSkia)> done) {
  const gfx::Size size = layer->bounds().size();
  layer->RequestCopyOfOutput(std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(
          [](const gfx::Size& size, const gfx::Rect& area,
             base::OnceCallback<void(gfx::ImageSkia)> done,
             std::unique_ptr<viz::CopyOutputResult> result) {
            if (result->IsEmpty() || size.IsEmpty()) {
              std::move(done).Run(gfx::ImageSkia());
              return;
            }
            const auto access = result->ScopedAccessSkBitmap();
            const SkBitmap& frame = access.bitmap();
            const float scale =
                static_cast<float>(frame.width()) / size.width();
            gfx::Rect pixels = gfx::ScaleToEnclosingRect(area, scale);
            pixels.Intersect(gfx::Rect(frame.width(), frame.height()));
            SkBitmap part;
            if (pixels.IsEmpty() ||
                !part.tryAllocPixels(
                    frame.info().makeWH(pixels.width(), pixels.height())) ||
                !frame.readPixels(part.pixmap(), pixels.x(), pixels.y())) {
              std::move(done).Run(gfx::ImageSkia());
              return;
            }
            part.setImmutable();
            std::move(done).Run(gfx::ImageSkia::CreateFromBitmap(part, scale));
          },
          size, area, std::move(done))));
}

// A view on its own layer that sits over others, leaves events to them, and
// is left out of layout.
void MakeOverlay(views::View* view) {
  view->SetPaintToLayer();
  view->layer()->SetFillsBoundsOpaquely(false);
  view->SetCanProcessEventsWithinSubtree(false);
  view->SetProperty(views::kViewIgnoredByLayoutKey, true);
  view->GetViewAccessibility().SetIsIgnored(true);
}

}  // namespace

// Covers the part of the sidebar that slides while it moves: the tint, as it
// blends, then the two sidebars side by side.
class SlideView : public views::View {
  METADATA_HEADER(SlideView, views::View)

 public:
  explicit SlideView(BrowserView* browser_view) : browser_view_(browser_view) {
    MakeOverlay(this);
  }

  // `leaving` is the whole tab strip as it is, whose top is `strip_top` here.
  void SetLeaving(const gfx::ImageSkia& leaving, int strip_top) {
    leaving_ = leaving;
    strip_top_ = strip_top;
    SchedulePaint();
  }

  // `coming` is the other workspace's tab strip, placed as `leaving` is.
  // Without it, only the tint comes in.
  void SetComing(const gfx::ImageSkia& coming) {
    coming_ = coming;
    SchedulePaint();
  }

  void Move(float offset, int direction) {
    offset_ = offset;
    direction_ = direction;
    SchedulePaint();
  }

  // Lets the pictures go.
  void Clear() {
    leaving_ = gfx::ImageSkia();
    SetComing(gfx::ImageSkia());
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    // Over glass the frame draws the tint as it blends, behind this.
    if (!ShowsGlass(browser_view_->browser())) {
      PaintTint(canvas, this, browser_view_,
                GetColorProvider()->GetColor(ui::kColorFrameActive),
                ThemeOfWindow(browser_view_->browser()));
    }
    const int x = base::ClampRound(offset_);
    canvas->DrawImageInt(leaving_, x, strip_top_);
    if (direction_ != 0 && !coming_.isNull()) {
      canvas->DrawImageInt(coming_, x + direction_ * width(), strip_top_);
    }
  }

 private:
  const raw_ptr<BrowserView> browser_view_;
  gfx::ImageSkia leaving_;
  gfx::ImageSkia coming_;
  int strip_top_ = 0;
  float offset_ = 0;
  int direction_ = 0;
};

BEGIN_METADATA(SlideView)
END_METADATA

// The page of the window a switch left, over the page of the window it
// showed, until it fades.
class PageCover : public views::View {
  METADATA_HEADER(PageCover, views::View)

 public:
  // The cover takes the page's own `corners`.
  PageCover(const gfx::ImageSkia& page, const gfx::RoundedCornersF& corners)
      : page_(page) {
    MakeOverlay(this);
    layer()->SetRoundedCornerRadius(corners);
    layer()->SetIsFastRoundedCorner(true);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawImageInt(page_, 0, 0);
  }

 private:
  const gfx::ImageSkia page_;
};

BEGIN_METADATA(PageCover)
END_METADATA

// Runs `done` once `page` has drawn or stopped loading, and at the latest
// after `wait`.
class PageWait : public content::WebContentsObserver {
 public:
  PageWait(content::WebContents* page,
           base::TimeDelta wait,
           base::OnceClosure done)
      : content::WebContentsObserver(page), done_(std::move(done)) {
    timer_.Start(FROM_HERE, wait,
                 base::BindOnce(&PageWait::Done, base::Unretained(this)));
  }

  // content::WebContentsObserver:
  void DidFirstVisuallyNonEmptyPaint() override { Done(); }
  void DidStopLoading() override { Done(); }
  void WebContentsDestroyed() override { Done(); }

 private:
  // `done` may let this go, so it runs later, unless this went first.
  void Done() {
    Observe(nullptr);
    timer_.Stop();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PageWait::Run, weak_factory_.GetWeakPtr()));
  }
  void Run() {
    if (done_) {
      std::move(done_).Run();
    }
  }

  base::OnceClosure done_;
  base::OneShotTimer timer_;
  base::WeakPtrFactory<PageWait> weak_factory_{this};
};

DEFINE_USER_DATA(WorkspaceSwipe);

WorkspaceSwipe::WorkspaceSwipe(BrowserWindowInterface* window,
                               BrowserView* browser_view,
                               TabStripRegionView* region)
    : window_(window),
      browser_view_(browser_view),
      region_(region),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  region_->AddPreTargetHandler(this);
  observations_.AddObservation(region_);
  observations_.AddObservation(browser_view_);
  if (WorkspaceWindow* shown = WorkspaceWindow::From(window_)) {
    shown->SetSlide(base::BindRepeating(&WorkspaceSwipe::SlideTo,
                                        base::Unretained(this)));
  }
  window_->GetTabStripModel()->AddObserver(this);
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget_observation_.Observe(widget);
  }
  workspaces_changed_ = WorkspaceList::Get().Observe(
      base::BindRepeating(&WorkspaceSwipe::ScheduleReady,
                          base::Unretained(this), kReadyAfterShowing));
  ScheduleReady(kReadyAfterShowing);
}

WorkspaceSwipe::~WorkspaceSwipe() {
  if (WorkspaceWindow* shown = WorkspaceWindow::From(window_)) {
    shown->SetSlide({});
  }
  if (browser_view_) {
    SetTintBlend(browser_view_, SK_ColorTRANSPARENT, Theme(), 1.0f, 0);
  }
  if (region_) {
    region_->RemovePreTargetHandler(this);
    if (SlideView* slide_view = slide_view_) {
      slide_view_ = nullptr;
      region_->RemoveChildViewT(slide_view);
    }
  }
}

// static
WorkspaceSwipe* WorkspaceSwipe::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

// static
void WorkspaceSwipe::GetReadyInTestsForTesting(bool ready) {
  g_ready_in_tests = ready;
}

void WorkspaceSwipe::SlideTo(const std::string& id) {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  const WorkspaceList& list = WorkspaceList::Get();
  const int from = shown ? list.IndexOf(shown->workspace()) : -1;
  const int to = list.IndexOf(id);
  if (!shown || shown->hidden() || shown->switching() || sliding() ||
      !region_ || from < 0 || to < 0 || from == to) {
    return;
  }
  const int direction = to > from ? 1 : -1;
  if (!SidebarCanSlide() || gfx::Animation::PrefersReducedMotion()) {
    // Only the page fades.
    Begin(/*with_picture=*/false);
    target_ = id;
    direction_ = direction;
    Land(base::TimeDelta());
    return;
  }
  Begin(/*with_picture=*/true);
  Aim(id, direction);
  after_leaving_ = base::BindOnce(&WorkspaceSwipe::Land,
                                  weak_factory_.GetWeakPtr(), kKeySlide);
  give_up_.Start(FROM_HERE, kGiveUp,
                 base::BindOnce(&WorkspaceSwipe::Reset,
                                base::Unretained(this)));
}

void WorkspaceSwipe::CoverPage(const gfx::ImageSkia& page) {
  if (page.isNull() || !browser_view_) {
    return;
  }
  OnUncovered();
  cover_ = browser_view_->AddChildView(std::make_unique<PageCover>(
      page, browser_view_->multi_contents_view()->GetBackgroundRadii()));
  ++covers_;
  PlaceCover();
}

void WorkspaceSwipe::UncoverPage() {
  if (!cover_ || !browser_view_) {
    return;
  }
  // A page that is still loading would show blank, so the cover stays over
  // it until it draws.
  content::WebContents* page = browser_view_->GetActiveWebContents();
  if (page && page->IsLoading() &&
      !page->CompletedFirstVisuallyNonEmptyPaint()) {
    Observe(page);
    cover_wait_.Start(
        FROM_HERE, kCoverWait,
        base::BindOnce(&WorkspaceSwipe::FadeCover, base::Unretained(this)));
    return;
  }
  FadeCover();
}

void WorkspaceSwipe::FadeCover() {
  Observe(nullptr);
  cover_wait_.Stop();
  if (!cover_ || !browser_view_) {
    return;
  }
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget->LayoutRootViewIfNecessary();
  }
  PlaceCover();
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&WorkspaceSwipe::PostUncovered,
                              weak_factory_.GetWeakPtr(), covers_))
      .OnAborted(base::BindOnce(&WorkspaceSwipe::PostUncovered,
                                weak_factory_.GetWeakPtr(), covers_))
      .Once()
      .SetDuration(kFade)
      .SetOpacity(cover_.get(), 0.0f, gfx::Tween::EASE_OUT);
}

void WorkspaceSwipe::RetakePicture(base::OnceClosure taken) {
  views::View* strip = TabStrip();
  if (!strip || !strip->layer()) {
    std::move(taken).Run();
    return;
  }
  // A window that has not shown since launch is laid out only now.
  if (views::Widget* widget = strip->GetWidget()) {
    widget->LayoutRootViewIfNecessary();
  }
  Copy(strip->layer(), strip->GetLocalBounds(),
       base::BindOnce(
           [](base::WeakPtr<WorkspaceSwipe> self, base::OnceClosure taken,
              gfx::ImageSkia picture) {
             if (self && !picture.isNull()) {
               self->picture_ = std::move(picture);
               self->stale_ = false;
             }
             std::move(taken).Run();
           },
           weak_factory_.GetWeakPtr(), std::move(taken)));
}

bool WorkspaceSwipe::shows_slide() const {
  return slide_view_ && slide_view_->GetVisible();
}

void WorkspaceSwipe::OnScrollEvent(ui::ScrollEvent* event) {
  if (event->type() != ui::EventType::kScroll) {
    return;
  }
  const auto consume = [event] {
    event->SetHandled();
    event->StopPropagation();
  };
  // A scroll no view handles goes to each view up the tree in turn, and so
  // comes through here again.
  if (event->time_stamp() == last_time_ &&
      event->momentum_phase() == last_phase_) {
    return;
  }
  last_time_ = event->time_stamp();
  last_phase_ = event->momentum_phase();
  switch (event->momentum_phase()) {
    case ui::EventMomentumPhase::MAY_BEGIN:
      // macOS starts each trackpad scroll with this, as the fingers touch.
      gesture_ = CanSwipe() ? Gesture::kPending : Gesture::kNone;
      pending_x_ = 0;
      pending_y_ = 0;
      return;
    case ui::EventMomentumPhase::BEGAN:
    case ui::EventMomentumPhase::INERTIAL_UPDATE:
    case ui::EventMomentumPhase::BLOCKED:
      if (gesture_ == Gesture::kTail) {
        consume();
      }
      return;
    case ui::EventMomentumPhase::END:
      // Both lifting the fingers and the end of the momentum after it.
      if (gesture_ == Gesture::kTracking) {
        consume();
        gesture_ = Gesture::kTail;
        Release(event->time_stamp());
        return;
      }
      if (gesture_ == Gesture::kTail) {
        consume();
      }
      gesture_ = Gesture::kNone;
      return;
    case ui::EventMomentumPhase::NONE:
      break;
  }
  switch (gesture_) {
    case Gesture::kPending:
      pending_x_ += FingerDelta(*event);
      pending_y_ += event->y_offset();
      if (std::abs(pending_y_) >= kAxisLock &&
          std::abs(pending_y_) > std::abs(pending_x_)) {
        gesture_ = Gesture::kVertical;
        return;
      }
      if (std::abs(pending_x_) < kAxisLock ||
          std::abs(pending_x_) <= std::abs(pending_y_)) {
        return;
      }
      consume();
      gesture_ = Gesture::kTracking;
      Begin(/*with_picture=*/true);
      Track(pending_x_, event->time_stamp());
      return;
    case Gesture::kTracking:
      consume();
      Track(FingerDelta(*event), event->time_stamp());
      return;
    case Gesture::kTail:
      consume();
      return;
    case Gesture::kNone:
    case Gesture::kVertical:
      return;
  }
}

void WorkspaceSwipe::AnimationProgressed(const gfx::Animation* animation) {
  SetOffset(gfx::Tween::FloatValueBetween(
      gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                 animation_.GetCurrentValue()),
      animate_from_, animate_to_));
}

void WorkspaceSwipe::AnimationEnded(const gfx::Animation* animation) {
  SetOffset(animate_to_);
  if (animation_done_) {
    std::move(animation_done_).Run();
  }
}

void WorkspaceSwipe::DidFirstVisuallyNonEmptyPaint() {
  FadeCover();
}

void WorkspaceSwipe::DidStopLoading() {
  FadeCover();
}

void WorkspaceSwipe::OnViewBoundsChanged(views::View* view) {
  // A new size makes the pictures of the windows behind this one old.
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  if (view == region_.get() && shown && !shown->hidden() && !sliding()) {
    resize_timer_.Start(
        FROM_HERE, kStaleAfter,
        base::BindOnce(&WorkspaceSwipe::OnResized, base::Unretained(this)));
  }
}

void WorkspaceSwipe::OnViewHierarchyWillBeDeleted(views::View* view) {
  // The window's views are going, BrowserView's children before BrowserView
  // itself, so nothing here may touch any of them after.
  animation_.Stop();
  give_up_.Stop();
  cover_wait_.Stop();
  StopReady();
  stale_timer_.Stop();
  resize_timer_.Stop();
  workspaces_changed_ = {};
  widget_observation_.Reset();
  Observe(nullptr);
  if (region_) {
    region_->RemovePreTargetHandler(this);
  }
  if (browser_view_) {
    SetTintBlend(browser_view_, SK_ColorTRANSPARENT, Theme(), 1.0f, 0);
  }
  slide_view_ = nullptr;
  hidden_.clear();
  layered_.clear();
  cover_ = nullptr;
  region_ = nullptr;
  browser_view_ = nullptr;
  observations_.RemoveAllObservations();
}

bool WorkspaceSwipe::SidebarCanSlide() const {
  if (!region_ || !browser_view_ || !TabStrip() || !TabStrip()->layer()) {
    return false;
  }
  if (const tabs::VerticalTabStripStateController* controller =
          tabs::VerticalTabStripStateController::From(window_.get());
      controller && controller->IsCollapsed()) {
    return false;
  }
  WorkspaceSwitcher* switcher = WorkspaceSwitcher::From(window_);
  return !switcher || !switcher->card();
}

bool WorkspaceSwipe::CanSwipe() const {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  return shown && !shown->hidden() && !shown->switching() && !sliding() &&
         SidebarCanSlide();
}

std::string WorkspaceSwipe::NeighborToward(int direction) const {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  const WorkspaceList& list = WorkspaceList::Get();
  const int index = list.IndexOf(shown->workspace()) + direction;
  if (index < 0 || index >= static_cast<int>(list.workspaces().size())) {
    return std::string();
  }
  return list.workspaces()[index].id;
}

views::View* WorkspaceSwipe::TabStrip() const {
  return region_ ? region_->GetTabStripView() : nullptr;
}

WorkspaceWindow* WorkspaceSwipe::PartnerFor(const std::string& id) const {
  if (id.empty()) {
    return nullptr;
  }
  for (WorkspaceWindow* partner : WorkspaceWindow::From(window_)->Partners()) {
    if (partner->workspace() == id) {
      return partner;
    }
  }
  return nullptr;
}

gfx::Rect WorkspaceSwipe::SlidingArea(const std::string& target) const {
  views::View* strip = TabStrip();
  gfx::Point origin;
  views::View::ConvertPointToTarget(strip, region_, &origin);
  gfx::Rect area(origin, strip->size());
  // The strip is only as tall as its tabs, and the other workspace may have
  // more, so the slide goes on down over the empty space below them.
  if (auto* vertical =
          views::AsViewClass<VerticalTabStripRegionView>(region_.get())) {
    area.set_height(
        std::max(area.height(),
                 vertical->GetBottomContainer()->bounds().bottom() - area.y()));
  }
  // Within a profile the pins stay, since both workspaces have them.
  const WorkspaceList::Workspace* workspace = WorkspaceList::Get().Find(target);
  const bool same_profile =
      !workspace || workspace->profile == window_->GetProfile()->GetBaseName();
  auto* tabs = views::AsViewClass<TabStripView>(strip);
  views::View* pins = tabs && tabs->GetPinnedTabsContainer()
                          ? views::ScrollView::GetScrollViewForContents(
                                tabs->GetPinnedTabsContainer())
                          : nullptr;
  if (same_profile && pins && pins->GetVisible()) {
    gfx::Point bottom(0, pins->height());
    views::View::ConvertPointToTarget(pins, region_, &bottom);
    area.Inset(gfx::Insets::TLBR(
        std::clamp(bottom.y() - area.y(), 0, area.height()), 0, 0, 0));
  }
  return area;
}

void WorkspaceSwipe::Begin(bool with_picture) {
  StopReady();
  phase_ = Phase::kTracking;
  ++slide_;
  target_.clear();
  preloaded_.clear();
  direction_ = 0;
  offset_ = 0;
  fingers_ = 0;
  samples_.clear();
  leaving_ = gfx::ImageSkia();
  landed_ = false;
  page_ready_ = false;
  page_ = gfx::ImageSkia();
  if (!with_picture) {
    return;
  }
  views::View* strip = TabStrip();
  Copy(strip->layer(), strip->GetLocalBounds(),
       base::BindOnce(&WorkspaceSwipe::OnLeavingPicture,
                      weak_factory_.GetWeakPtr(), slide_));
}

void WorkspaceSwipe::OnLeavingPicture(int slide, gfx::ImageSkia picture) {
  if (slide != slide_ || !sliding() || !region_) {
    return;
  }
  leaving_ = std::move(picture);
  // Without a picture the real sidebar stays, and only the page fades.
  if (!leaving_.isNull()) {
    picture_ = leaving_;
    stale_ = false;
    if (!slide_view_) {
      slide_view_ =
          region_->AddChildView(std::make_unique<SlideView>(browser_view_));
    }
    region_->ReorderChildView(slide_view_, region_->children().size());
    ShowSlide();
  }
  if (after_leaving_) {
    std::move(after_leaving_).Run();
  }
}

void WorkspaceSwipe::ShowSlide() {
  if (!slide_view_ || leaving_.isNull()) {
    return;
  }
  slide_view_->SetBoundsRect(area_);
  gfx::Point strip_top;
  views::View::ConvertPointToTarget(TabStrip(), region_, &strip_top);
  slide_view_->SetLeaving(leaving_, strip_top.y() - area_.y());

  // The other workspace's sidebar, or only its tint where there is no
  // picture.
  WorkspaceWindow* partner = PartnerFor(target_);
  WorkspaceSwipe* other = partner ? From(partner->window()) : nullptr;
  slide_view_->SetComing(other ? other->picture() : gfx::ImageSkia());
  slide_view_->Move(offset_, target_.empty() ? 0 : direction_);
  slide_view_->SetVisible(true);
  HideTabsUnderSlide(ShowsGlass(window_));
}

void WorkspaceSwipe::HideTabsUnderSlide(bool hide) {
  if (!hide) {
    for (views::View* view : layered_) {
      view->DestroyLayer();
    }
    for (views::View* view : hidden_) {
      if (view->layer()) {
        view->layer()->SetOpacity(1);
      }
    }
    layered_.clear();
    hidden_.clear();
    return;
  }
  auto* tabs = views::AsViewClass<TabStripView>(TabStrip());
  if (!tabs || !hidden_.empty()) {
    return;
  }
  for (views::View* contents :
       {static_cast<views::View*>(tabs->GetPinnedTabsContainer()),
        static_cast<views::View*>(tabs->GetUnpinnedTabsContainer())}) {
    views::View* area =
        contents ? views::ScrollView::GetScrollViewForContents(contents)
                 : nullptr;
    if (!area || !area->GetVisible() ||
        !views::View::ConvertRectToTarget(area->parent(), region_,
                                          area->bounds())
             .Intersects(area_)) {
      continue;
    }
    if (!area->layer()) {
      area->SetPaintToLayer();
      area->layer()->SetFillsBoundsOpaquely(false);
      layered_.push_back(area);
    }
    area->layer()->SetOpacity(0);
    hidden_.push_back(area);
  }
}

void WorkspaceSwipe::Aim(const std::string& target, int direction) {
  target_ = target;
  direction_ = direction;
  area_ = SlidingArea(target);
  ShowSlide();
  SetOffset(offset_);
  // The other workspace's window goes in under this one, so its sidebar slides
  // in as it is now and its page is drawn by the time it lands.
  WorkspaceWindow* partner =
      target.empty() ? nullptr
                     : WorkspaceWindow::From(window_)->Prepare(target);
  if (WorkspaceSwipe* other = partner ? From(partner->window()) : nullptr) {
    other->RetakePicture(base::BindOnce(&WorkspaceSwipe::OnComingPicture,
                                        weak_factory_.GetWeakPtr(), slide_,
                                        target));
  }
}

void WorkspaceSwipe::OnComingPicture(int slide, const std::string& target) {
  if (slide == slide_ && target == target_) {
    ShowSlide();
  }
}

void WorkspaceSwipe::Track(float fingers, base::TimeTicks time) {
  fingers_ += fingers;
  const int direction = fingers_ < 0 ? 1 : -1;
  if (direction != direction_) {
    Aim(NeighborToward(direction), direction);
  }
  // The other workspace's sidebar stops where it lands, however far the
  // fingers go on.
  const float width = area_.width();
  fingers_ = std::clamp(fingers_, -width, width);
  // Past either end the sidebar only stretches a little.
  const float offset =
      target_.empty() ? kStretch * fingers_ / (std::abs(fingers_) + kStretch)
                      : fingers_;
  SetOffset(offset);
  if (!target_.empty() && target_ != preloaded_ &&
      -direction_ * offset_ > area_.width() / 2) {
    preloaded_ = target_;
    WorkspaceWindow::From(window_)->Preload(target_);
  }
  samples_.push_back({time, offset});
  std::erase_if(samples_, [&](const Sample& sample) {
    return time - sample.time > kSpeedWindow;
  });
}

void WorkspaceSwipe::Release(base::TimeTicks time) {
  float speed = 0;
  if (samples_.size() > 1) {
    const float ms = (samples_.back().time - samples_.front().time)
                         .InMillisecondsF();
    if (ms > 0) {
      speed = (samples_.back().offset - samples_.front().offset) / ms;
    }
  }
  const float width = area_.width();
  const float distance = -direction_ * offset_;
  const bool flick = speed * offset_ > 0 && std::abs(speed) >= kFlickSpeed &&
                     distance >= kFlickDistance;
  if (target_.empty() || distance <= 0 || width <= 0 ||
      (distance <= width / 2 && !flick)) {
    SnapBack();
    return;
  }
  Land(std::max(kShortestLand, kLand * (1 - distance / width)));
}

void WorkspaceSwipe::SetOffset(float offset) {
  offset_ = offset;
  if (slide_view_ && slide_view_->GetVisible()) {
    slide_view_->Move(offset_, target_.empty() ? 0 : direction_);
  }
  const float width = area_.width();
  const float amount = target_.empty() || width <= 0
                           ? 0
                           : std::min(1.0f, -direction_ * offset_ / width);
  // The tint blends toward the other workspace's once its window is open.
  WorkspaceWindow* partner = PartnerFor(target_);
  BrowserView* other =
      partner ? BrowserView::GetBrowserViewForBrowser(partner->window())
              : nullptr;
  if (browser_view_ && other && other->GetColorProvider()) {
    const Theme other_theme = ThemeOfWindow(partner->window());
    SetTintBlend(
        browser_view_,
        other->GetColorProvider()->GetColor(ui::kColorFrameActive), other_theme,
        ShowsGlass(window_) ? WindowCover(other_theme.transparency) : 1.0f,
        amount);
  } else if (browser_view_) {
    SetTintBlend(browser_view_, SK_ColorTRANSPARENT, Theme(), 1.0f, 0);
  }
  if (WorkspaceSwitcher* switcher = WorkspaceSwitcher::From(window_)) {
    switcher->SetSlide(target_, std::max(0.0f, amount));
  }
  RepaintTint();
}

void WorkspaceSwipe::Animate(float to,
                             base::TimeDelta duration,
                             base::OnceClosure then) {
  animation_.Stop();
  animate_from_ = offset_;
  animate_to_ = to;
  animation_done_ = std::move(then);
  animation_.SetDuration(duration);
  animation_.Start();
}

void WorkspaceSwipe::SnapBack() {
  phase_ = Phase::kSnapping;
  Animate(0, kSnapBack,
          base::BindOnce(&WorkspaceSwipe::Reset, base::Unretained(this)));
}

void WorkspaceSwipe::Land(base::TimeDelta duration) {
  phase_ = Phase::kLanding;
  give_up_.Start(FROM_HERE, kGiveUp,
                 base::BindOnce(&WorkspaceSwipe::Reset,
                                base::Unretained(this)));
  views::View* page = browser_view_->multi_contents_view();
  Copy(browser_view_->GetWidget()->GetLayer(),
       page->ConvertRectToWidget(page->GetLocalBounds()),
       base::BindOnce(&WorkspaceSwipe::OnPagePicture,
                      weak_factory_.GetWeakPtr(), slide_));
  if (duration.is_zero()) {
    landed_ = true;
    return;
  }
  Animate(-direction_ * area_.width(), duration,
          base::BindOnce(
              [](WorkspaceSwipe* self) {
                self->landed_ = true;
                self->ArriveIfReady();
              },
              base::Unretained(this)));
}

void WorkspaceSwipe::OnPagePicture(int slide, gfx::ImageSkia page) {
  if (slide != slide_ || phase_ != Phase::kLanding) {
    return;
  }
  page_ = std::move(page);
  page_ready_ = true;
  ArriveIfReady();
}

void WorkspaceSwipe::ArriveIfReady() {
  if (phase_ != Phase::kLanding || !landed_ || !page_ready_) {
    return;
  }
  landed_ = false;
  give_up_.Start(FROM_HERE, kGiveUp,
                 base::BindOnce(&WorkspaceSwipe::Reset,
                                base::Unretained(this)));
  WorkspaceWindow::From(window_)->WithPartner(
      target_, base::BindOnce(&WorkspaceSwipe::SwitchTo,
                              weak_factory_.GetWeakPtr(), page_));
}

void WorkspaceSwipe::SwitchTo(gfx::ImageSkia page,
                              BrowserWindowInterface* partner) {
  base::WeakPtr<WorkspaceSwipe> other;
  if (WorkspaceSwipe* swipe = From(partner); swipe && swipe != this) {
    swipe->CoverPage(page);
    other = swipe->weak_factory_.GetWeakPtr();
  }
  // The other window draws under this one, with the cover, before it comes
  // to the front; until then this window keeps the slide where it landed.
  const bool switching = WorkspaceWindow::From(window_)->Switch(
      target_, base::BindOnce(
                   [](base::WeakPtr<WorkspaceSwipe> self,
                      base::WeakPtr<WorkspaceSwipe> other,
                      BrowserWindowInterface* shown) {
                     if (other) {
                       other->UncoverPage();
                     }
                     if (self) {
                       self->Reset();
                     }
                   },
                   weak_factory_.GetWeakPtr(), other));
  if (!switching) {
    if (other) {
      other->OnUncovered();
    }
    Reset();
  }
}

void WorkspaceSwipe::PlaceCover() {
  views::View* contents = browser_view_->multi_contents_view();
  gfx::Point origin;
  views::View::ConvertPointToTarget(contents, browser_view_, &origin);
  cover_->SetBoundsRect(gfx::Rect(origin, contents->size()));
}

void WorkspaceSwipe::PostUncovered(int cover) {
  // An aborted animation may not start another, so the cover goes later.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<WorkspaceSwipe> self, int cover) {
                       if (self && cover == self->covers_) {
                         self->OnUncovered();
                       }
                     },
                     weak_factory_.GetWeakPtr(), cover));
}

void WorkspaceSwipe::OnUncovered() {
  Observe(nullptr);
  cover_wait_.Stop();
  if (!browser_view_) {
    return;
  }
  if (PageCover* cover = cover_) {
    cover_ = nullptr;
    browser_view_->RemoveChildViewT(cover);
  }
}

void WorkspaceSwipe::Reset() {
  animation_.Stop();
  give_up_.Stop();
  ++slide_;
  phase_ = Phase::kNone;
  target_.clear();
  direction_ = 0;
  offset_ = 0;
  fingers_ = 0;
  samples_.clear();
  leaving_ = gfx::ImageSkia();
  page_ = gfx::ImageSkia();
  landed_ = false;
  page_ready_ = false;
  after_leaving_.Reset();
  animation_done_.Reset();
  if (slide_view_) {
    slide_view_->SetVisible(false);
    slide_view_->Clear();
  }
  HideTabsUnderSlide(false);
  if (browser_view_) {
    SetTintBlend(browser_view_, SK_ColorTRANSPARENT, Theme(), 1.0f, 0);
  }
  if (WorkspaceSwitcher* switcher = WorkspaceSwitcher::From(window_)) {
    switcher->SetSlide(std::string(), 0);
  }
  RepaintTint();
  if (WorkspaceWindow* shown = WorkspaceWindow::From(window_)) {
    shown->Unprepare();
  }
  ScheduleReady(kReadyAfterShowing);
}

void WorkspaceSwipe::RepaintTint() {
  // Each view keeps its last drawing until it is told to draw again, so each
  // one that draws the tint is told: the frame, the background around the
  // page, the sidebar, the toolbar's top corners, and the corners between
  // them.
  if (region_) {
    region_->SchedulePaint();
  }
  if (!browser_view_ || !browser_view_->GetWidget()) {
    return;
  }
  browser_view_->toolbar()->SchedulePaint();
  if (views::NonClientView* frame =
          browser_view_->GetWidget()->non_client_view();
      frame && frame->frame_view()) {
    frame->frame_view()->SchedulePaint();
  }
  const auto repaint_tint_views = [](views::View* parent) {
    for (views::View* child : parent->children()) {
      if (views::IsViewClass<MainBackgroundRegionView>(child) ||
          views::IsViewClass<CustomFloatingCorner>(child)) {
        child->SchedulePaint();
      }
    }
  };
  repaint_tint_views(browser_view_);
  repaint_tint_views(browser_view_->multi_contents_view());
}

void WorkspaceSwipe::OnWidgetVisibilityChanged(views::Widget* widget,
                                               bool visible) {
  if (visible) {
    ScheduleReady(kReadyAfterShowing);
  }
}

void WorkspaceSwipe::OnWidgetDestroying(views::Widget* widget) {
  StopReady();
  widget_observation_.Reset();
}

void WorkspaceSwipe::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  OnSidebarChanged();
}

void WorkspaceSwipe::OnTabChangedAt(tabs::TabInterface* tab,
                                    TabChangeType change_type) {
  // A picture is taken once the page stops loading, so only what stays
  // afterward counts.
  if (change_type != TabChangeType::kLoadingOnly) {
    OnSidebarChanged();
  }
}

void WorkspaceSwipe::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                             int index) {
  OnSidebarChanged();
}

void WorkspaceSwipe::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  OnSidebarChanged();
}

void WorkspaceSwipe::OnTabGroupChanged(const TabGroupChange& change) {
  OnSidebarChanged();
}

void WorkspaceSwipe::ScheduleReady(base::TimeDelta delay) {
  tried_.clear();
  ready_timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&WorkspaceSwipe::GetReady, base::Unretained(this)));
}

bool WorkspaceSwipe::CanGetReady() const {
#if BUILDFLAG(IS_MAC)
  if (!g_ready_in_tests &&
      base::CommandLine::ForCurrentProcess()->HasSwitch("test-type")) {
    return false;
  }
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  return shown && !shown->hidden() && !shown->switching() && !sliding() &&
         region_ && browser_view_ && window_->GetWindow()->IsVisible();
#else
  // Windows shows a window put under another on its taskbar, so workspaces
  // get ready there once hidden windows do; see docs/plans/windows.md.
  return false;
#endif
}

void WorkspaceSwipe::GetReady() {
  if (ready_wait_ || ready_give_up_.IsRunning() || !CanGetReady()) {
    return;
  }
  // The page in front draws first.
  content::WebContents* page = browser_view_->GetActiveWebContents();
  if (page && page->IsLoading() &&
      !page->CompletedFirstVisuallyNonEmptyPaint()) {
    ready_wait_ =
        std::make_unique<PageWait>(page, kReadyPageWait,
                                   base::BindOnce(
                                       [](base::WeakPtr<WorkspaceSwipe> self) {
                                         if (self) {
                                           self->ready_wait_.reset();
                                           self->ReadyNext();
                                         }
                                       },
                                       weak_factory_.GetWeakPtr()));
    return;
  }
  ReadyNext();
}

void WorkspaceSwipe::ReadyNext() {
  if (ready_wait_ || ready_give_up_.IsRunning() || !CanGetReady()) {
    return;
  }
  const std::string id = NextToReady();
  if (id.empty()) {
    return;
  }
  tried_.insert(id);
  const int step = ++ready_step_;
  ready_give_up_.Start(FROM_HERE, kReadyGiveUp,
                       base::BindOnce(&WorkspaceSwipe::EndReadyStep,
                                      base::Unretained(this), step));
  WorkspaceWindow::From(window_)->WithPartner(
      id, base::BindOnce(&WorkspaceSwipe::OnReadyPartner,
                         weak_factory_.GetWeakPtr(), step, id));
}

std::string WorkspaceSwipe::NextToReady() const {
  const WorkspaceList& list = WorkspaceList::Get();
  const int here = list.IndexOf(WorkspaceWindow::From(window_)->workspace());
  const int count = static_cast<int>(list.workspaces().size());
  for (int distance = 1; here >= 0 && distance < count; ++distance) {
    for (int index : {here + distance, here - distance}) {
      if (index < 0 || index >= count) {
        continue;
      }
      const std::string& id = list.workspaces()[index].id;
      if (tried_.contains(id)) {
        continue;
      }
      WorkspaceWindow* partner = PartnerFor(id);
      WorkspaceSwipe* other = partner ? From(partner->window()) : nullptr;
      if (!partner || (other && other->NeedsReady())) {
        return id;
      }
    }
  }
  return std::string();
}

void WorkspaceSwipe::OnReadyPartner(int step,
                                    const std::string& id,
                                    BrowserWindowInterface* window) {
  if (step != ready_step_ || !ready_give_up_.IsRunning()) {
    return;
  }
  WorkspaceSwipe* other =
      CanGetReady() && window != window_.get() ? From(window) : nullptr;
  if (!other) {
    EndReadyStep(step);
    return;
  }
  if (!WorkspaceWindow::From(window_)->PrepareSidebar(id)) {
    EndReadyStep(step);
    return;
  }
  other->RetakePicture(base::BindOnce(&WorkspaceSwipe::EndReadyStep,
                                      weak_factory_.GetWeakPtr(), step));
}

void WorkspaceSwipe::EndReadyStep(int step) {
  if (step != ready_step_ || !ready_give_up_.IsRunning()) {
    return;
  }
  ++ready_step_;
  ready_give_up_.Stop();
  ready_wait_.reset();
  if (WorkspaceWindow* shown = WorkspaceWindow::From(window_);
      shown && !sliding()) {
    shown->Unprepare();
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceSwipe::GetReady, weak_factory_.GetWeakPtr()));
}

void WorkspaceSwipe::StopReady() {
  ++ready_step_;
  ready_give_up_.Stop();
  ready_timer_.Stop();
  ready_wait_.reset();
}

bool WorkspaceSwipe::NeedsReady() const {
  return picture_.isNull() || stale_;
}

void WorkspaceSwipe::OnSidebarChanged() {
  const WorkspaceWindow* self = WorkspaceWindow::From(window_);
  if (self && self->hidden() && !picture_.isNull()) {
    stale_timer_.Start(
        FROM_HERE, kStaleAfter,
        base::BindOnce(&WorkspaceSwipe::OnStale, base::Unretained(this)));
  }
}

void WorkspaceSwipe::OnStale() {
  stale_ = true;
  const WorkspaceWindow* self = WorkspaceWindow::From(window_);
  for (WorkspaceWindow* partner :
       self ? self->Partners() : std::vector<WorkspaceWindow*>()) {
    WorkspaceSwipe* shown =
        partner->hidden() ? nullptr : From(partner->window());
    if (shown) {
      shown->ScheduleReady(base::TimeDelta());
    }
  }
}

void WorkspaceSwipe::OnResized() {
  const WorkspaceWindow* self = WorkspaceWindow::From(window_);
  for (WorkspaceWindow* partner :
       self ? self->Partners() : std::vector<WorkspaceWindow*>()) {
    if (WorkspaceSwipe* other = From(partner->window());
        other && !other->picture_.isNull()) {
      other->stale_ = true;
    }
  }
  ScheduleReady(base::TimeDelta());
}

}  // namespace uw
