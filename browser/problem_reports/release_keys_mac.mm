// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/release_keys.h"

#import <Foundation/Foundation.h>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace uw {
namespace {

std::string InfoString(NSString* key) {
  NSString* value = base::apple::ObjCCast<NSString>(
      [base::apple::OuterBundle() objectForInfoDictionaryKey:key]);
  return value ? base::SysNSStringToUTF8(value) : std::string();
}

}  // namespace

ReleaseKeys GetReleaseKeys() {
  return {
      .crash_url = InfoString(@"UWCrashReportURL"),
      .posthog_key = InfoString(@"UWPostHogKey"),
      .posthog_host = InfoString(@"UWPostHogHost"),
  };
}

}  // namespace uw
