// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/tab_organization/page_copy.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace uw {

std::unique_ptr<content::WebContents> CopyHistory(content::WebContents* page,
                                                  bool needs_reload) {
  content::WebContents::CreateParams params(page->GetBrowserContext());
  params.initially_hidden = true;
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  std::unique_ptr<content::WebContents> copy =
      content::WebContents::Create(params);
  copy->GetController().CopyStateFrom(&page->GetController(), needs_reload);
  return copy;
}

bool SameHistory(content::WebContents* a, content::WebContents* b) {
  content::NavigationController& one = a->GetController();
  content::NavigationController& two = b->GetController();
  if (one.GetEntryCount() != two.GetEntryCount() ||
      one.GetLastCommittedEntryIndex() != two.GetLastCommittedEntryIndex()) {
    return false;
  }
  content::NavigationEntry* first = one.GetLastCommittedEntry();
  content::NavigationEntry* second = two.GetLastCommittedEntry();
  if (!first || !second) {
    return first == second;
  }
  const content::FaviconStatus& icon = first->GetFavicon();
  const content::FaviconStatus& other = second->GetFavicon();
  return first->GetURL() == second->GetURL() &&
         first->GetTitle() == second->GetTitle() && icon.valid == other.valid &&
         icon.image.AsImageSkia().BackedBySameObjectAs(
             other.image.AsImageSkia());
}

void TakePicture(content::WebContents* page,
                 base::OnceCallback<void(gfx::ImageSkia)> taken) {
  content::RenderWidgetHostView* view = page->GetRenderWidgetHostView();
  if (!view || !view->IsSurfaceAvailableForCopy() ||
      page->GetVisibility() != content::Visibility::VISIBLE) {
    return;
  }
  view->CopyFromSurface(
      gfx::Rect(), gfx::Size(), base::TimeDelta(),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](base::OnceCallback<void(gfx::ImageSkia)> taken, float scale,
             const content::CopyFromSurfaceResult& result) {
            if (result.has_value() && !result->bitmap.drawsNothing()) {
              std::move(taken).Run(
                  gfx::ImageSkia::CreateFromBitmap(result->bitmap, scale));
            }
          },
          std::move(taken), view->GetDeviceScaleFactor())));
}

}  // namespace uw
