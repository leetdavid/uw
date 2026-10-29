// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TAB_ORGANIZATION_PAGE_COPY_H_
#define UW_BROWSER_TAB_ORGANIZATION_PAGE_COPY_H_

#include <memory>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace gfx {
class ImageSkia;
}

namespace uw {

// A page with `page`'s history and no renderer. With `needs_reload`, it loads
// that history once it shows. A pin's placeholder and a shared tab's copy in
// another window are both one.
std::unique_ptr<content::WebContents> CopyHistory(content::WebContents* page,
                                                  bool needs_reload);

// Whether `a` and `b` show the same history, title, and icon.
bool SameHistory(content::WebContents* a, content::WebContents* b);

// Gives `taken` a picture of `page` as it looks now, for the hazed view other
// windows show once it leaves. Nothing comes when `page` is not on screen.
void TakePicture(content::WebContents* page,
                 base::OnceCallback<void(gfx::ImageSkia)> taken);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_PAGE_COPY_H_
