// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/mac/mac_util.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "uw/browser/ui/glass/glass_prefs.h"

// A glass view that leaves clicks to the views over it, as Chromium's window
// glass does.
API_AVAILABLE(macos(26.0))
@interface UwFrostedGlassView : NSGlassEffectView
@end

@implementation UwFrostedGlassView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace uw {
namespace {

// Keeps frosted glass behind a card's widget, inside the card's shadow, until
// the widget goes, and then deletes itself.
class FrostedGlass : public views::WidgetObserver {
 public:
  FrostedGlass(views::Widget* widget, const gfx::Insets& insets, int radius)
      : insets_(insets) {
    observation_.Observe(widget);
    if (@available(macOS 26.0, *)) {
      NSView* content =
          widget->GetNativeWindow().GetNativeNSWindow().contentView;
      UwFrostedGlassView* glass =
          [[UwFrostedGlassView alloc] initWithFrame:NSZeroRect];
      glass.style = NSGlassEffectViewStyleRegular;
      glass.cornerRadius = radius;
      [content addSubview:glass positioned:NSWindowBelow relativeTo:nil];
      glass_ = glass;
      Place();
    }
  }

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override {
    Place();
  }

  void OnWidgetDestroying(views::Widget* widget) override {
    observation_.Reset();
    [glass_ removeFromSuperview];
    delete this;
  }

 private:
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

  const gfx::Insets insets_;
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
  new FrostedGlass(widget, insets, corner_radius);
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

}  // namespace uw
