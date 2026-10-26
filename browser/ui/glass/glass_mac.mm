// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/mac/mac_util.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/class_property.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_host.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_scroll_view_container.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "uw/browser/ui/glass/glass_prefs.h"
#include "uw/browser/ui/look/menu_card.h"

// A glass view that leaves clicks to the views over it, as Chromium's window
// glass does, cut to its rounded shape, with a hole near its top that it keeps
// as it resizes.
API_AVAILABLE(macos(26.0))
@interface UwFrostedGlassView : NSGlassEffectView
@property(nonatomic) std::optional<uw::GlassHole> hole;
@end

@implementation UwFrostedGlassView {
  std::optional<uw::GlassHole> _hole;
}

- (NSView*)hitTest:(NSPoint)point {
  return nil;
}

- (std::optional<uw::GlassHole>)hole {
  return _hole;
}

- (void)setHole:(std::optional<uw::GlassHole>)hole {
  _hole = hole;
  [self cut];
}

- (void)setCornerRadius:(CGFloat)cornerRadius {
  [super setCornerRadius:cornerRadius];
  [self cut];
}

- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  [self cut];
}

// Masks the glass to its rounded shape, and the hole out of it, so what is
// under the hole stays sharp. The glass's shadow falls outside that shape, and
// its window ends at the glass's square edge, so unmasked the shadow showed in
// the window's four corners.
- (void)cut {
  const CGRect glass = self.bounds;
  const CGFloat corner = std::min<CGFloat>(
      self.cornerRadius,
      std::min(CGRectGetWidth(glass), CGRectGetHeight(glass)) / 2);
  CGMutablePathRef path = CGPathCreateMutable();
  CGPathAddRoundedRect(path, nullptr, glass, corner, corner);
  if (_hole) {
    [self addHole:path];
  }
  CAShapeLayer* mask = [CAShapeLayer layer];
  mask.fillRule = kCAFillRuleEvenOdd;
  mask.frame = glass;
  mask.path = path;
  CGPathRelease(path);
  self.layer.mask = mask;
}

- (void)addHole:(CGMutablePathRef)path {
  const CGRect glass = self.bounds;
  const CGRect hole = CGRectMake(
      _hole->sides, CGRectGetHeight(glass) - _hole->top - _hole->height,
      std::max<CGFloat>(0, CGRectGetWidth(glass) - 2 * _hole->sides),
      _hole->height);
  const CGFloat radius = std::min<CGFloat>(
      _hole->radius, std::min(CGRectGetWidth(hole), CGRectGetHeight(hole)) / 2);
  CGPathAddRoundedRect(path, nullptr, hole, radius, radius);
}
@end

namespace uw {
namespace {

// Keeps frosted glass behind a card's widget, inside the card's shadow, until
// the widget goes or the glass is taken away, and then deletes itself.
class FrostedGlass : public views::WidgetObserver {
 public:
  // The glass behind `widget`, or null.
  static FrostedGlass* Of(views::Widget* widget) {
    const auto found = All().find(widget);
    return found == All().end() ? nullptr : found->second;
  }

  explicit FrostedGlass(views::Widget* widget) : widget_(widget) {
    All()[widget] = this;
    observation_.Observe(widget);
    if (@available(macOS 26.0, *)) {
      UwFrostedGlassView* glass =
          [[UwFrostedGlassView alloc] initWithFrame:NSZeroRect];
      glass.wantsLayer = YES;
      glass.style = NSGlassEffectViewStyleRegular;
      glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
      [widget->GetNativeWindow().GetNativeNSWindow().contentView
          addSubview:glass
          positioned:NSWindowBelow
          relativeTo:nil];
      glass_ = glass;
    }
  }

  void Set(const gfx::Insets& insets,
           int corner_radius,
           const std::optional<GlassHole>& hole) {
    insets_ = insets;
    Place();
    if (@available(macOS 26.0, *)) {
      UwFrostedGlassView* glass = static_cast<UwFrostedGlassView*>(glass_);
      glass.cornerRadius = corner_radius;
      glass.hole = hole;
    }
  }

  void Remove() {
    observation_.Reset();
    All().erase(widget_);
    [glass_ removeFromSuperview];
    delete this;
  }

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override {
    Place();
  }

  void OnWidgetDestroying(views::Widget* widget) override { Remove(); }

 private:
  static std::map<views::Widget*, FrostedGlass*>& All() {
    static base::NoDestructor<std::map<views::Widget*, FrostedGlass*>> all;
    return *all;
  }

  void Place() {
    NSView* content = glass_.superview;
    if (!content) {
      return;
    }
    const NSRect bounds = content.bounds;
    glass_.frame =
        NSMakeRect(insets_.left(), insets_.bottom(),
                   std::max<CGFloat>(0, NSWidth(bounds) - insets_.width()),
                   std::max<CGFloat>(0, NSHeight(bounds) - insets_.height()));
  }

  const raw_ptr<views::Widget> widget_;
  gfx::Insets insets_;
  NSView* __strong glass_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

// Tells its subscribers when a rule changes, and watches only while it has
// some, so nothing outlives the browser's local state.
class GlassRules {
 public:
  static GlassRules& Get() {
    static base::NoDestructor<GlassRules> rules;
    return *rules;
  }

  base::CallbackListSubscription Add(base::RepeatingClosure callback) {
    if (callbacks_.empty()) {
      Watch();
    }
    return callbacks_.Add(std::move(callback));
  }

 private:
  friend class base::NoDestructor<GlassRules>;

  GlassRules() {
    callbacks_.set_removal_callback(base::BindRepeating(
        &GlassRules::StopWhenUnwatched, base::Unretained(this)));
  }

  void Watch() {
    if (PrefService* local_state = g_browser_process->local_state()) {
      registrar_.Init(local_state);
      registrar_.Add(
          kGlassBehindPref,
          base::BindRepeating(&GlassRules::Notify, base::Unretained(this)));
    }
    observer_ = [NSWorkspace.sharedWorkspace.notificationCenter
        addObserverForName:
            NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  GlassRules::Get().Notify();
                }];
  }

  void StopWhenUnwatched() {
    if (!callbacks_.empty()) {
      return;
    }
    registrar_.Reset();
    if (observer_) {
      [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer_];
      observer_ = nil;
    }
  }

  void Notify() { callbacks_.Notify(); }

  PrefChangeRegistrar registrar_;
  id __strong observer_;
  base::RepeatingClosureList callbacks_;
};

// Set on the first item of each menu uw frosts.
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kFrostedMenuKey, false)

// Lays Chromium's menu color at 85% over frosted glass in `window`, which
// shows `submenu`.
void FrostMenuWindow(views::Widget* window, views::SubmenuView* submenu) {
  submenu->SetBorderColorId(kColorMenuGlass);
  submenu->GetScrollViewContainer()->SchedulePaint();
  AddFrostedGlass(window, gfx::Insets(),
                  views::MenuConfig::instance().CornerRadiusForMenu(
                      submenu->GetMenuItem()->GetMenuController()));
}

void OnMenuWindowMade(views::Widget* window, views::SubmenuView* submenu) {
  if (submenu->GetMenuItem()->GetRootMenuItem()->GetProperty(kFrostedMenuKey)) {
    FrostMenuWindow(window, submenu);
  }
}

}  // namespace

bool CanShowGlass() {
  return base::mac::MacOSMajorVersion() >= 26;
}

bool ReducesTransparency() {
  return NSWorkspace.sharedWorkspace
      .accessibilityDisplayShouldReduceTransparency;
}

base::CallbackListSubscription OnGlassRulesChanged(
    base::RepeatingClosure callback) {
  return GlassRules::Get().Add(std::move(callback));
}

void AddFrostedGlass(views::Widget* widget,
                     const gfx::Insets& insets,
                     int corner_radius) {
  SetFrostedGlass(widget, true, insets, corner_radius, std::nullopt);
}

void SetFrostedGlass(views::Widget* widget,
                     bool on,
                     const gfx::Insets& insets,
                     int corner_radius,
                     const std::optional<GlassHole>& hole) {
  FrostedGlass* glass = FrostedGlass::Of(widget);
  if (!on) {
    if (glass) {
      glass->Remove();
    }
    return;
  }
  if (!glass) {
    glass = new FrostedGlass(widget);
  }
  glass->Set(insets, corner_radius, hole);
}

void FrostPageBubble(views::Widget* widget, content::WebContents* contents) {
  if (!CanShowGlass() || !widget || !contents) {
    return;
  }
  contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
  views::BubbleDialogDelegate* bubble =
      widget->widget_delegate()->AsBubbleDialogDelegate();
  bubble->SetBackgroundColor(SK_ColorTRANSPARENT);
  AddFrostedGlass(widget,
                  bubble->GetBubbleFrameView()->bubble_border()->GetInsets(),
                  bubble->GetCornerRadius());
}

void FrostOpenMenu(BrowserWindowInterface* window) {
  views::MenuController* controller =
      views::MenuController::GetActiveInstance();
  if (!window || ElementGlassOfWindow(window) == 0 || !controller ||
      !controller->GetSelectedMenuItem()) {
    return;
  }
  static bool hooked = false;
  if (!hooked) {
    views::MenuHost::SetMadeCallback(base::BindRepeating(&OnMenuWindowMade));
    hooked = true;
  }
  views::MenuItemView* menu =
      controller->GetSelectedMenuItem()->GetRootMenuItem();
  menu->SetProperty(kFrostedMenuKey, true);
  views::SubmenuView* submenu = menu->GetSubmenu();
  if (submenu && submenu->GetWidget()) {
    FrostMenuWindow(submenu->GetWidget(), submenu);
  }
}

}  // namespace uw
