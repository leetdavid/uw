// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/second_click.h"

namespace uw {

// Only macOS acts on a double-click on the sidebar's empty space so far.
bool IsSecondClick() {
  return false;
}

}  // namespace uw
