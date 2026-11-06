// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TRANSLATION_TRANSLATION_BUBBLE_H_
#define UW_BROWSER_UI_VIEWS_TRANSLATION_TRANSLATION_BUBBLE_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace views {
class View;
}

namespace uw {

// What uw adds to Chromium's translate bubble, per "While a page is
// translated" in docs/plans/page-translation.md.

// The line under the bubble's language tabs while this device downloads a
// language for the translation, or empty.
std::u16string TranslationDownloadText(content::WebContents* web_contents);

// The error view's title for the page's last translation problem, or empty to
// keep Chromium's own.
std::u16string TranslationProblemTitle(content::WebContents* web_contents);

// The engine's own words about the problem, or empty.
std::u16string TranslationProblemMessage(content::WebContents* web_contents);

// Fills `buttons` for the page's last translation problem: when the engine
// lacks the page's language, a button per other engine that is set up, which
// translates the page with it this once, or "Set up another engine…" when
// none is, which opens the settings and calls `close_bubble`. Returns whether
// it added any.
bool AddOtherEngineButtons(content::WebContents* web_contents,
                           views::View* buttons,
                           base::RepeatingClosure close_bubble);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TRANSLATION_TRANSLATION_BUBBLE_H_
