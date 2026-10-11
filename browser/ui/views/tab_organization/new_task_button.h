// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_TASK_BUTTON_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_TASK_BUTTON_H_

class BrowserWindowInterface;

namespace views {
class Button;
}

namespace uw {

// Makes the sidebar's new tab button open its tab in a new task of its own,
// and says so in its tooltip.
void OpenNewTaskFrom(views::Button* button, BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_TASK_BUTTON_H_
