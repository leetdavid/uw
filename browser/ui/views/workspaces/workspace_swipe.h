// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWIPE_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWIPE_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;
class BrowserWindowInterface;
class TabStripRegionView;

namespace ui {
class ScrollEvent;
}

namespace uw {

class PageCover;
class SlideView;
class WorkspaceWindow;

// The slide between workspaces in a window's sidebar, per
// docs/plans/workspaces.md. A two-finger swipe on the wide sidebar moves its
// name row and tabs with the fingers, and its pins too toward another
// profile, while the tint blends toward the other workspace's. Once the
// fingers pass halfway, the other workspace's page starts to load. Past
// halfway, or on a flick, the other workspace lands: its window takes this
// one's place and its page settles in as this page fades out. Short of that,
// the sidebar slides back. Ctrl+1 to 9, Cmd+Option+arrows, and a click on a
// workspace play the same slide on their own.
//
// The other workspace's sidebar is a picture of its window from when it last
// slid away. A window that has not slid away since launch has no picture, so
// only its workspace's name slides in, and its tabs show as it lands.
class WorkspaceSwipe : public ui::EventHandler,
                       public gfx::AnimationDelegate,
                       public views::ViewObserver {
 public:
  DECLARE_USER_DATA(WorkspaceSwipe);

  // `region` is the sidebar of `browser_view`, whose scrolls it watches.
  WorkspaceSwipe(BrowserWindowInterface* window,
                 BrowserView* browser_view,
                 TabStripRegionView* region);
  WorkspaceSwipe(const WorkspaceSwipe&) = delete;
  WorkspaceSwipe& operator=(const WorkspaceSwipe&) = delete;
  ~WorkspaceSwipe() override;

  static WorkspaceSwipe* From(BrowserWindowInterface* window);

  // Slides to workspace `id` on its own, then switches. With Reduce Motion,
  // or while the sidebar is hidden, only the page fades.
  void SlideTo(const std::string& id);

  // The tab strip as it was when this window last slid away. Null until then.
  const gfx::ImageSkia& picture() const { return picture_; }

  // Shows `page`, the page of the window this one takes over from, over this
  // window's page, which waits 12px toward `direction`.
  void CoverPage(const gfx::ImageSkia& page, int direction);
  // Fades the cover out as it drifts 12px with the slide, while this window's
  // page settles in.
  void UncoverPage();

  bool sliding() const { return phase_ != Phase::kNone; }
  // How far the sidebar has moved: negative toward the next workspace.
  float offset() const { return offset_; }
  // The workspace the slide heads to. Empty past either end.
  const std::string& target() const { return target_; }
  bool has_cover() const { return !!cover_; }
  // Whether the sliding pictures cover the sidebar.
  bool shows_slide() const;

  // ui::EventHandler:
  void OnScrollEvent(ui::ScrollEvent* event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // views::ViewObserver:
  void OnViewHierarchyWillBeDeleted(views::View* view) override;

 private:
  enum class Gesture {
    kNone,
    // The fingers are down but have not moved far enough to tell the axis.
    kPending,
    // Scrolls the tabs, which see every event until the fingers lift.
    kVertical,
    // Moves the sidebar.
    kTracking,
    // The fingers have lifted. The momentum macOS sends after is swallowed.
    kTail,
  };

  enum class Phase {
    kNone,
    // Following the fingers, or waiting for the sidebar's picture.
    kTracking,
    // Going back to where it started.
    kSnapping,
    // Going on to the other workspace, then switching.
    kLanding,
  };

  struct Sample {
    base::TimeTicks time;
    float offset;
  };

  // Whether the wide sidebar is there to slide, with nothing open on it.
  bool SidebarCanSlide() const;
  // Whether a swipe may start now.
  bool CanSwipe() const;
  // The workspace next to this window's toward `direction`: 1 for the next,
  // -1 for the previous. Empty at either end.
  std::string NeighborToward(int direction) const;
  views::View* TabStrip() const;
  // This window's partner for workspace `id`, if it is open.
  WorkspaceWindow* PartnerFor(const std::string& id) const;
  // The part of the sidebar that slides toward `target`, in `region_`.
  gfx::Rect SlidingArea(const std::string& target) const;

  // Starts a slide, with a picture of the tab strip as it is now unless only
  // the page fades.
  void Begin(bool with_picture);
  void OnLeavingPicture(int slide, gfx::ImageSkia picture);
  void ShowSlide();
  // Points the slide at `target`, `direction` from here.
  void Aim(const std::string& target, int direction);
  void Track(float fingers, base::TimeTicks time);
  void Release(base::TimeTicks time);
  void SetOffset(float offset);
  // Animates the offset to `to`, then runs `then`.
  void Animate(float to, base::TimeDelta duration, base::OnceClosure then);
  void SnapBack();
  // Goes on to the target over `duration`, takes a picture of the page, and
  // switches once both are done.
  void Land(base::TimeDelta duration);
  void OnPagePicture(int slide, gfx::ImageSkia page);
  void ArriveIfReady();
  void SwitchTo(gfx::ImageSkia page,
                int direction,
                BrowserWindowInterface* partner);
  void PlaceCover();
  // Takes cover number `cover` away once its fade ends, unless another took
  // its place.
  void PostUncovered(int cover);
  void OnUncovered();
  // Over glass the slide draws no tint of its own, so the tabs under it are
  // hidden while it shows, and shown again after.
  void HideTabsUnderSlide(bool hide);
  // Ends the slide and puts the sidebar back as it was.
  void Reset();
  void RepaintTint();

  const raw_ptr<BrowserWindowInterface> window_;
  // Null once their views go.
  raw_ptr<BrowserView> browser_view_;
  raw_ptr<TabStripRegionView> region_;

  Gesture gesture_ = Gesture::kNone;
  // The last scroll seen, which comes again for each view up the tree when
  // none handles it.
  base::TimeTicks last_time_;
  ui::EventMomentumPhase last_phase_ = ui::EventMomentumPhase::NONE;
  float pending_x_ = 0;
  float pending_y_ = 0;
  // How far the fingers have moved since the swipe began.
  float fingers_ = 0;
  std::vector<Sample> samples_;

  Phase phase_ = Phase::kNone;
  // Counts slides, so a picture that comes in late for an older one is left.
  int slide_ = 0;
  std::string target_;
  // The workspace whose page this swipe last started to load.
  std::string preloaded_;
  int direction_ = 0;
  float offset_ = 0;
  gfx::Rect area_;
  gfx::ImageSkia leaving_;
  gfx::ImageSkia picture_;
  raw_ptr<SlideView> slide_view_ = nullptr;
  // The tab areas hidden under the slide, and those given a layer to hide.
  std::vector<raw_ptr<views::View>> hidden_;
  std::vector<raw_ptr<views::View>> layered_;
  // What a key's slide does once the tab strip's picture is in.
  base::OnceClosure after_leaving_;

  gfx::LinearAnimation animation_{this};
  float animate_from_ = 0;
  float animate_to_ = 0;
  base::OnceClosure animation_done_;
  bool landed_ = false;
  bool page_ready_ = false;
  gfx::ImageSkia page_;
  // Gives up waiting for the other workspace's window, which opens once its
  // profile loads.
  base::OneShotTimer give_up_;

  raw_ptr<PageCover> cover_ = nullptr;
  int cover_direction_ = 0;
  int covers_ = 0;
  // Set when the fade gave the page a layer it did not have.
  bool page_layer_made_ = false;

  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
  ui::ScopedUnownedUserData<WorkspaceSwipe> user_data_;
  base::WeakPtrFactory<WorkspaceSwipe> weak_factory_{this};
};

// How far the fingers moved along `event`, rightward positive, whichever way
// the user's scrolling goes.
float FingerDelta(const ui::ScrollEvent& event);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWIPE_H_
