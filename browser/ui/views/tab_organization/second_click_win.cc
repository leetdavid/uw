// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/second_click.h"

#include <windows.h>

#include "ui/events/event.h"

namespace uw {

// Windows asks the views about both clicks of a double-click on space that
// drags the window, and acts on the second only if they leave it, so that
// space stays the window's. The message says which click it is.
bool IsSecondClick(const ui::MouseEvent* event) {
  if (!event || !event->HasNativeEvent()) {
    return false;
  }
  const UINT message = event->native_event().message;
  return message == WM_LBUTTONDBLCLK || message == WM_NCLBUTTONDBLCLK;
}

}  // namespace uw
