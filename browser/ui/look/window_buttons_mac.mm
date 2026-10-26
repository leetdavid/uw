// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/window_buttons.h"

#import <AppKit/AppKit.h>

#include <algorithm>

#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace uw {
namespace {

// Seen through the button above it, this is a mid gray on a light tint.
constexpr float kDotOpacity = 0.45f;

}  // namespace

void PaintWindowButtonDots(gfx::Canvas* canvas,
                           const views::View* view,
                           SkColor color) {
  const views::Widget* widget = view->GetWidget();
  if (!widget || widget->ShouldPaintAsActive()) {
    return;
  }
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content = window.contentView;
  if (!content) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(color, kDotOpacity * 255));
  for (NSWindowButton kind :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:kind];
    // In full screen the buttons belong to another window.
    if (!button || button.hidden || button.window != window) {
      continue;
    }
    const NSRect frame = [content convertRect:button.bounds fromView:button];
    gfx::RectF dot(NSMinX(frame),
                   content.flipped ? NSMinY(frame)
                                   : NSHeight(content.bounds) - NSMaxY(frame),
                   NSWidth(frame), NSHeight(frame));
    views::View::ConvertRectToTarget(widget->GetRootView(), view, &dot);
    // Half a point inside the button's own circle, so no dark rim shows.
    canvas->DrawCircle(dot.CenterPoint(),
                       std::min(dot.width(), dot.height()) / 2 - 0.5f, flags);
  }
}

void SetWindowButtonsHidden(views::Widget* widget, bool hidden) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  for (NSWindowButton kind :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [window standardWindowButton:kind].hidden = hidden;
  }
}

void PressWindowButton(views::Widget* widget,
                       WindowButton button,
                       bool option) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  switch (button) {
    case WindowButton::kClose:
      [window performClose:nil];
      break;
    case WindowButton::kMinimize:
      [window performMiniaturize:nil];
      break;
    case WindowButton::kZoom:
      if (option) {
        [window performZoom:nil];
      } else {
        [window toggleFullScreen:nil];
      }
      break;
  }
}

}  // namespace uw
