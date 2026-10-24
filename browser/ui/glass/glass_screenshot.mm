// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass_screenshot.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <utility>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "skia/ext/skia_utils_mac.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

// How far the desktop picture reaches past the window on each side.
constexpr CGFloat kDesktopMargin = 40;

NSWindow* WindowOf(Browser* browser) {
  return browser->GetWindow()->GetNativeWindow().GetNativeNSWindow();
}

// A window shows before its first frame has drawn, and glass settles after.
void WaitForDrawing() {
  base::RunLoop drawn;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, drawn.QuitClosure(), base::Milliseconds(500));
  drawn.Run();
}

// This process's windows and the displays they are on.
API_AVAILABLE(macos(14.4))
SCShareableContent* OwnContent() {
  __block SCShareableContent* own = nil;
  base::RunLoop found;
  __block base::OnceClosure done = found.QuitClosure();
  [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:^(
                          SCShareableContent* content, NSError* error) {
    own = content;
    std::move(done).Run();
  }];
  found.Run();
  return own;
}

API_AVAILABLE(macos(14.4))
SCWindow* Find(SCShareableContent* content, NSWindow* window) {
  for (SCWindow* each in content.windows) {
    if (each.windowID == window.windowNumber) {
      return each;
    }
  }
  return nil;
}

API_AVAILABLE(macos(14.4))
SkBitmap Shoot(SCContentFilter* filter, SCStreamConfiguration* config) {
  __block SkBitmap pixels;
  base::RunLoop shot;
  __block base::OnceClosure done = shot.QuitClosure();
  config.showsCursor = NO;
  [SCScreenshotManager
      captureImageWithFilter:filter
               configuration:config
           completionHandler:^(CGImageRef image, NSError* error) {
             if (image) {
               pixels = skia::CGImageToSkBitmap(image);
             }
             std::move(done).Run();
           }];
  shot.Run();
  return pixels;
}

}  // namespace

void SaveWindowWithPopups(Browser* browser, const std::string& name) {
  if (@available(macOS 14.4, *)) {
    WaitForDrawing();
    SCWindow* window = Find(OwnContent(), WindowOf(browser));
    ASSERT_TRUE(window);
    SCContentFilter* filter =
        [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.width = window.frame.size.width * filter.pointPixelScale;
    config.height = window.frame.size.height * filter.pointPixelScale;
    config.includeChildWindows = YES;
    const SkBitmap pixels = Shoot(filter, config);
    ASSERT_FALSE(pixels.drawsNothing());
    SaveBitmap(pixels, name);
  }
}

void SaveWindowOverDesktop(Browser* browser, const std::string& name) {
  SaveNativeWindowOverDesktop(browser->GetWindow()->GetNativeWindow(), name);
}

void SaveNativeWindowOverDesktop(gfx::NativeWindow native_window,
                                 const std::string& name) {
  if (@available(macOS 14.4, *)) {
    NSWindow* window = native_window.GetNativeNSWindow();
    const NSRect area =
        NSInsetRect(window.frame, -kDesktopMargin, -kDesktopMargin);
    NSWindow* desktop =
        [[NSWindow alloc] initWithContentRect:area
                                    styleMask:NSWindowStyleMaskBorderless
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    desktop.releasedWhenClosed = NO;
    NSView* picture = desktop.contentView;
    picture.wantsLayer = YES;
    picture.layer.contents = [[NSImage alloc]
        initWithContentsOfFile:@"/System/Library/Desktop Pictures/Sonoma.heic"];
    picture.layer.contentsGravity = kCAGravityResizeAspectFill;
    [desktop orderWindow:NSWindowBelow relativeTo:window.windowNumber];
    WaitForDrawing();

    SCShareableContent* content = OwnContent();
    SCWindow* front = Find(content, window);
    SCWindow* back = Find(content, desktop);
    // ScreenCaptureKit measures from the top left of the main screen.
    const CGFloat main_height = NSScreen.screens.firstObject.frame.size.height;
    const CGRect shot = CGRectMake(area.origin.x, main_height - NSMaxY(area),
                                   area.size.width, area.size.height);
    SCDisplay* display = nil;
    for (SCDisplay* each in content.displays) {
      if (CGRectContainsPoint(each.frame, CGPointMake(CGRectGetMidX(shot),
                                                      CGRectGetMidY(shot)))) {
        display = each;
      }
    }
    if (!front || !back || !display) {
      [desktop close];
      FAIL() << "The window, its desktop, or its screen is not shareable.";
    }
    SCContentFilter* filter =
        [[SCContentFilter alloc] initWithDisplay:display
                                includingWindows:@[ back, front ]];
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.sourceRect =
        CGRectOffset(shot, -display.frame.origin.x, -display.frame.origin.y);
    config.width = area.size.width * filter.pointPixelScale;
    config.height = area.size.height * filter.pointPixelScale;
    const SkBitmap pixels = Shoot(filter, config);
    [desktop close];
    ASSERT_FALSE(pixels.drawsNothing());
    SaveBitmap(pixels, name);
  }
}

}  // namespace uw
