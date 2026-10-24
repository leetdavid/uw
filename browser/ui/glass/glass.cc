// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "components/prefs/pref_service.h"
#include "uw/browser/ui/glass/glass_prefs.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

base::RepeatingCallback<bool(BrowserWindowInterface*)>& Query() {
  static base::NoDestructor<
      base::RepeatingCallback<bool(BrowserWindowInterface*)>>
      query;
  return *query;
}

}  // namespace

bool WantsGlass(BrowserWindowInterface* window) {
  return CanShowGlass() && !ReducesTransparency() &&
         ThemeOfWindow(window).transparency > 0;
}

size_t MaxGlassWindows() {
  const PrefService* local_state = g_browser_process->local_state();
  return local_state && !local_state->GetBoolean(kGlassBehindPref)
             ? 1
             : std::numeric_limits<size_t>::max();
}

bool ShowsGlass(BrowserWindowInterface* window) {
  return Query() && Query().Run(window);
}

void SetGlassQuery(
    base::RepeatingCallback<bool(BrowserWindowInterface*)> query) {
  Query() = std::move(query);
}

float CoverOfWindow(BrowserWindowInterface* window) {
  return ShowsGlass(window) ? WindowCover(ThemeOfWindow(window).transparency)
                            : 1.0f;
}

int ElementGlassOfWindow(BrowserWindowInterface* window) {
  return CanShowGlass() && !ReducesTransparency() ? ThemeOfWindow(window).glass
                                                  : 0;
}

float ToolbarCoverOfWindow(BrowserWindowInterface* window, SkColor color) {
  if (!ShowsGlass(window)) {
    return 1.0f;
  }
  return std::max(WindowCover(ThemeOfWindow(window).transparency),
                  ReadableCover(color, TextColorOn(Tint{color, color})));
}

}  // namespace uw
