// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SECOND_CLICK_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SECOND_CLICK_H_

namespace ui {
class MouseEvent;
}

namespace uw {

// True while the second click of a double-click is down or coming up: the
// click `event` a view has, or, with no event, the click the window is asking
// about.
bool IsSecondClick(const ui::MouseEvent* event);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SECOND_CLICK_H_
