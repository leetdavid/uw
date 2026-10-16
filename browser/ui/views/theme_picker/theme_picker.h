// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_THEME_PICKER_THEME_PICKER_H_
#define UW_BROWSER_UI_VIEWS_THEME_PICKER_THEME_PICKER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

class BrowserWindowInterface;
class WebUIBubbleManager;

namespace content {
class WebContents;
}

namespace views {
class Widget;
}

namespace uw {

// A window's theme picker: a bubble beside the sidebar that holds the theme
// picker page. The sidebar's Organize bar owns it.
class ThemePicker {
 public:
  DECLARE_USER_DATA(ThemePicker);

  explicit ThemePicker(BrowserWindowInterface* window);
  ThemePicker(const ThemePicker&) = delete;
  ThemePicker& operator=(const ThemePicker&) = delete;
  ~ThemePicker();

  static ThemePicker* From(BrowserWindowInterface* window);

  // Shows the picker of `window`. False when the window has none.
  static bool Open(BrowserWindowInterface* window);

  bool IsShowing() const;
  // Null until the picker first opens.
  content::WebContents* page() const;
  views::Widget* GetWidgetForTesting() const;

 private:
  void Show();

  const raw_ptr<BrowserWindowInterface> window_;
  std::unique_ptr<WebUIBubbleManager> bubble_;
  ui::ScopedUnownedUserData<ThemePicker> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_THEME_PICKER_THEME_PICKER_H_
