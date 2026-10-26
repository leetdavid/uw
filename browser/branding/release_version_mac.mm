// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

#import <Foundation/Foundation.h>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/version_info/version_info.h"

namespace uw {

std::optional<std::string> ReleaseVersion() {
  NSString* version =
      base::apple::ObjCCast<NSString>([base::apple::OuterBundle()
          objectForInfoDictionaryKey:@"CFBundleShortVersionString"]);
  if (!version) {
    return std::nullopt;
  }
  std::string release = base::SysNSStringToUTF8(version);
  if (release == version_info::GetVersionNumber()) {
    return std::nullopt;
  }
  return release;
}

}  // namespace uw
