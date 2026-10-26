// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/run_window_mark.h"

#include "base/check.h"
#include "base/containers/flat_set.h"
#include "base/no_destructor.h"

namespace uw {

namespace {

bool g_making_run_window = false;

base::flat_set<const BrowserWindowInterface*>& RunWindows() {
  static base::NoDestructor<base::flat_set<const BrowserWindowInterface*>>
      windows;
  return *windows;
}

}  // namespace

bool IsRunWindow(const BrowserWindowInterface* window) {
  return RunWindows().contains(window);
}

bool IsMakingRunWindow() {
  return g_making_run_window;
}

ScopedMakingRunWindow::ScopedMakingRunWindow() {
  CHECK(!g_making_run_window);
  g_making_run_window = true;
}

ScopedMakingRunWindow::~ScopedMakingRunWindow() {
  g_making_run_window = false;
}

void MarkRunWindow(const BrowserWindowInterface* window, bool run_window) {
  if (run_window) {
    RunWindows().insert(window);
  } else {
    RunWindows().erase(window);
  }
}

}  // namespace uw
