// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/report_issue.h"

#include <optional>
#include <string>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/system/sys_info.h"
#include "build/build_config.h"
#include "url/gurl.h"
#include "uw/browser/branding/release_version.h"

namespace uw {

namespace {

#if BUILDFLAG(IS_WIN)
constexpr char kSystem[] = ", Windows ";
// Report an Issue opens the log's folder beside the new issue.
constexpr char kLogHint[] =
    "\n\nPlease attach chrome_debug.log from the folder uw opened.";
#else
constexpr char kSystem[] = ", macOS ";
constexpr char kLogHint[] = "";
#endif

}  // namespace

GURL NewIssueUrl() {
  std::optional<std::string> release = ReleaseVersion();
  std::string versions = base::StrCat(
      {release ? "uw " + *release : "uw development build", ", Chromium ",
       ChromiumVersion(), kSystem, base::SysInfo::OperatingSystemVersion()});
  return GURL(base::StrCat({"https://github.com/leetdavid/uw/issues/new?body=",
                            base::EscapeQueryParamValue(
                                base::StrCat({kLogHint, "\n\n---\n", versions}),
                                /*use_plus=*/true)}));
}

}  // namespace uw
