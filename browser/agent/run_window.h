// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_RUN_WINDOW_H_
#define UW_BROWSER_AGENT_RUN_WINDOW_H_

class Profile;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace uw {

// Opens a blank tab in `profile`'s run window, making the window when it has
// none. A run's agent opens its tabs from that tab, so they open in the run
// window too, out of sight, with no window of the user's needed; see
// IsRunWindow() and docs/product.md, "Scheduled chats". Null when Chromium
// cannot make the window.
tabs::TabInterface* OpenRunTab(Profile* profile);

// Closes `run_tab` and every tab opened from it, or from those, without
// adding them to Recently Closed. The run window closes with its last tab.
void CloseRunTabs(tabs::TabInterface& run_tab);

// Moves `tab`, a run's tab in the run window, into the window of `next_to`,
// just after it, without selecting it, so the user sees what a waiting step
// asks about. It stays there after the run ends.
void BringRunTab(tabs::TabInterface& tab, tabs::TabInterface& next_to);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_RUN_WINDOW_H_
