// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/terminal/close_guard.h"

namespace uw {

DEFINE_USER_DATA(CloseGuard);

CloseGuard::CloseGuard(ui::UnownedUserDataHost& host)
    : user_data_(host, *this) {}

CloseGuard::~CloseGuard() = default;

}  // namespace uw
