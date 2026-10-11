// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/window_screenshot.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/widget/widget.h"

namespace uw {
namespace {

constexpr char kFolderVariable[] = "UW_SCREENSHOT_DIR";
constexpr gfx::Rect kWindowBounds(0, 0, 1280, 800);
constexpr int kMaxFrames = 60;

SkBitmap Capture(views::Widget* widget) {
  widget->LayoutRootViewIfNecessary();
  base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> copied;
  widget->GetLayer()->RequestCopyOfOutput(
      std::make_unique<viz::CopyOutputRequest>(
          viz::CopyOutputRequest::ResultFormat::RGBA,
          viz::CopyOutputRequest::ResultDestination::kSystemMemory,
          copied.GetCallback()));
  const std::unique_ptr<viz::CopyOutputResult> result = copied.Take();
  EXPECT_FALSE(result->IsEmpty());
  const auto access = result->ScopedAccessSkBitmap();
  const SkBitmap frame = access.bitmap();
  SkBitmap pixels;
  pixels.allocPixels(frame.info());
  frame.readPixels(pixels.pixmap());
  return pixels;
}

// Returns the window once two frames in a row look the same, which means the
// views have finished drawing their latest state.
SkBitmap CaptureSettled(views::Widget* widget) {
  SkBitmap pixels = Capture(widget);
  for (int frames = 0; frames < kMaxFrames; ++frames) {
    SkBitmap next = Capture(widget);
    const bool settled = gfx::BitmapsAreEqual(pixels, next);
    pixels = std::move(next);
    if (settled) {
      break;
    }
  }
  return pixels;
}

}  // namespace

void SaveWindowScreenshot(Browser* browser, const std::string& name) {
  const std::optional<std::string> folder =
      base::Environment::Create()->GetVar(kFolderVariable);
  if (!folder || folder->empty()) {
    return;
  }
  views::Widget* widget =
      BrowserView::GetBrowserViewForBrowser(browser)->GetWidget();
  widget->SetBounds(kWindowBounds);
  const std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(CaptureSettled(widget),
                                        /*discard_transparency=*/false);
  ASSERT_TRUE(png);
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(*folder).AppendASCII(name + ".png"), *png));
}

}  // namespace uw
