// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PROBLEM_REPORTS_WEBUI_ERROR_REPORTS_H_
#define UW_BROWSER_PROBLEM_REPORTS_WEBUI_ERROR_REPORTS_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace uw {

// A JavaScript error on a Chromium page that asks development builds to crash
// on one, chiefly the address bar's list. uw's builds are not official builds,
// so Chromium crashes on these. A preview carries on instead and, after a Yes
// to "Help fix uw?", sends Sentry a report without a crash. uw dev and browser
// tests keep the crash. See docs/plans/distribution.md, "Build flags".
void SetUpWebUIErrorReports();

// What a report holds. The message is redacted as Chrome redacts it.
struct WebUIErrorReport {
  std::string page;
  // The script and line the error came from.
  std::string source;
  std::string message;
  std::string stack;
};

// Tests see each report as it is sent.
void SetWebUIErrorReportCallbackForTesting(
    base::RepeatingCallback<void(const WebUIErrorReport&)> callback);

}  // namespace uw

#endif  // UW_BROWSER_PROBLEM_REPORTS_WEBUI_ERROR_REPORTS_H_
