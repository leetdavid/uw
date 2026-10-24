// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/app_activity.h"

#import <AppKit/AppKit.h>

#include "base/no_destructor.h"

namespace uw {
namespace {

std::optional<bool>& ActiveForTesting() {
  static std::optional<bool> active;
  return active;
}

// Watches macOS's notices that uw came to the front or left it.
class AppActivity {
 public:
  static AppActivity& Get() {
    static base::NoDestructor<AppActivity> activity;
    return *activity;
  }

  base::CallbackListSubscription Add(base::RepeatingClosure callback) {
    return callbacks_.Add(std::move(callback));
  }

  void Notify() { callbacks_.Notify(); }

 private:
  friend class base::NoDestructor<AppActivity>;

  AppActivity() {
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name : {NSApplicationDidBecomeActiveNotification,
                                    NSApplicationDidResignActiveNotification}) {
      [center addObserverForName:name
                          object:nil
                           queue:nil
                      usingBlock:^(NSNotification*) {
                        AppActivity::Get().Notify();
                      }];
    }
  }

  base::RepeatingClosureList callbacks_;
};

}  // namespace

bool IsAppActive() {
  return ActiveForTesting().value_or(NSApp.active);
}

base::CallbackListSubscription ObserveAppActivity(
    base::RepeatingClosure callback) {
  return AppActivity::Get().Add(std::move(callback));
}

void SetAppActiveForTesting(std::optional<bool> active) {
  ActiveForTesting() = active;
  AppActivity::Get().Notify();
}

}  // namespace uw
