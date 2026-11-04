// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_PAGE_SPACE_PREFS_H_
#define UW_BROWSER_UI_LOOK_PAGE_SPACE_PREFS_H_

class PrefRegistrySimple;

namespace uw {

// "Space around the page" on uw's settings page, for all of uw. Off, the page
// card has no margin and meets the window's top, right, and bottom edges.
inline constexpr char kPageSpacePref[] = "uw.look.page_space";

void RegisterPageSpaceLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_PAGE_SPACE_PREFS_H_
