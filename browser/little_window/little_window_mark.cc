// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_mark.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/no_destructor.h"

namespace uw {

namespace {

std::optional<std::string>& Making() {
  static base::NoDestructor<std::optional<std::string>> making;
  return *making;
}

base::flat_map<const BrowserWindowInterface*, std::string>& LittleWindows() {
  static base::NoDestructor<
      base::flat_map<const BrowserWindowInterface*, std::string>>
      windows;
  return *windows;
}

base::flat_set<const void*>& Parts() {
  static base::NoDestructor<base::flat_set<const void*>> parts;
  return *parts;
}

}  // namespace

bool IsLittleWindow(const BrowserWindowInterface* window) {
  return LittleWindows().contains(window);
}

std::string LittleWindowWorkspace(const BrowserWindowInterface* window) {
  const auto it = LittleWindows().find(window);
  return it == LittleWindows().end() ? std::string() : it->second;
}

bool IsMakingLittleWindow() {
  return Making().has_value();
}

const std::string& MakingLittleWindowWorkspace() {
  CHECK(Making());
  return *Making();
}

ScopedMakingLittleWindow::ScopedMakingLittleWindow(std::string workspace) {
  CHECK(!Making());
  Making() = std::move(workspace);
}

ScopedMakingLittleWindow::~ScopedMakingLittleWindow() {
  Making().reset();
}

void MarkLittleWindow(const BrowserWindowInterface* window,
                      std::string workspace) {
  LittleWindows()[window] = std::move(workspace);
}

void UnmarkLittleWindow(const BrowserWindowInterface* window) {
  LittleWindows().erase(window);
}

void MarkLittleWindowPart(const void* part) {
  Parts().insert(part);
}

void UnmarkLittleWindowPart(const void* part) {
  Parts().erase(part);
}

bool IsLittleWindowPart(const void* part) {
  return Parts().contains(part);
}

}  // namespace uw
