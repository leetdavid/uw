// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/hidden_tabs.h"

namespace uw {

DEFINE_USER_DATA(HiddenTabs);

HiddenTabs::HiddenTabs(ui::UnownedUserDataHost& host)
    : user_data_(host, *this) {}

HiddenTabs::~HiddenTabs() = default;

}  // namespace uw
