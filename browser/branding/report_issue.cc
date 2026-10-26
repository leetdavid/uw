// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/report_issue.h"

#include <optional>
#include <string>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/system/sys_info.h"
#include "base/version_info/version_info.h"
#include "url/gurl.h"
#include "uw/browser/branding/release_version.h"

namespace uw {

GURL NewIssueUrl() {
  std::optional<std::string> release = ReleaseVersion();
  std::string versions =
      base::StrCat({release ? "uw " + *release : "uw development build",
                    ", Chromium ", version_info::GetVersionNumber(), ", macOS ",
                    base::SysInfo::OperatingSystemVersion()});
  return GURL(base::StrCat(
      {"https://github.com/leetdavid/uw/issues/new?body=",
       base::EscapeQueryParamValue(base::StrCat({"\n\n---\n", versions}),
                                   /*use_plus=*/true)}));
}

}  // namespace uw
