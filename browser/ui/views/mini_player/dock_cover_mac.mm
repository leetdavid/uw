// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/dock_cover.h"

#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>
#include <unistd.h>

#include <cstdint>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/widget/widget.h"

namespace uw {
namespace {

std::optional<double>& PartForTesting() {
  static std::optional<double> part;
  return part;
}

// A value of a window's entry in macOS's window list.
template <typename T>
T* Value(NSDictionary* window, CFStringRef key) {
  return base::apple::ObjCCast<T>(window[base::apple::CFToNSPtrCast(key)]);
}

}  // namespace

double CoveredPart(views::Widget* window, const gfx::Rect& area) {
  if (PartForTesting()) {
    return *PartForTesting();
  }
  if (!window || area.IsEmpty()) {
    return 0;
  }
  NSWindow* own = window->GetNativeWindow().GetNativeNSWindow();
  if (!(own.occlusionState & NSWindowOcclusionStateVisible)) {
    return 1;
  }
  // Other apps' window bounds need no screen-recording permission; only their
  // titles do.
  NSArray* above = base::apple::CFToNSOwnershipCast(
      CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenAboveWindow |
                                     kCGWindowListExcludeDesktopElements,
                                 static_cast<CGWindowID>(own.windowNumber)));
  SkRegion covered;
  for (NSDictionary* other in above) {
    if (Value<NSNumber>(other, kCGWindowLayer).intValue != 0 ||
        Value<NSNumber>(other, kCGWindowOwnerPID).intValue == getpid() ||
        Value<NSNumber>(other, kCGWindowAlpha).doubleValue == 0) {
      continue;
    }
    CGRect bounds;
    NSDictionary* frame = Value<NSDictionary>(other, kCGWindowBounds);
    if (!frame || !CGRectMakeWithDictionaryRepresentation(
                      base::apple::NSToCFPtrCast(frame), &bounds)) {
      continue;
    }
    gfx::Rect part(bounds);
    part.Intersect(area);
    covered.op(gfx::RectToSkIRect(part), SkRegion::kUnion_Op);
  }
  int64_t covered_area = 0;
  for (SkRegion::Iterator it(covered); !it.done(); it.next()) {
    covered_area += int64_t{it.rect().width()} * it.rect().height();
  }
  return static_cast<double>(covered_area) /
         (int64_t{area.width()} * area.height());
}

void SetCoveredPartForTesting(std::optional<double> part) {
  PartForTesting() = part;
}

}  // namespace uw
