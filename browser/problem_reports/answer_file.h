// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PROBLEM_REPORTS_ANSWER_FILE_H_
#define UW_BROWSER_PROBLEM_REPORTS_ANSWER_FILE_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"

namespace uw {

// The user's answer to "Help fix uw?". It lives in a file in Crashpad's
// folder, which never moves with --user-data-dir, so the crash reporter can
// read it when it starts, before anything else in the browser runs.
struct ProblemReportsAnswer {
  bool send = false;
  // A random ID that tells installs apart in PostHog. Empty unless `send`.
  std::string install_id;
};

// `crash_dumps` is Chromium's DIR_CRASH_DUMPS.
base::FilePath ProblemReportsAnswerFile(const base::FilePath& crash_dumps);

// Nothing until the user has answered.
std::optional<ProblemReportsAnswer> ReadProblemReportsAnswer(
    const base::FilePath& file);
bool WriteProblemReportsAnswer(const base::FilePath& file,
                               const ProblemReportsAnswer& answer);

}  // namespace uw

#endif  // UW_BROWSER_PROBLEM_REPORTS_ANSWER_FILE_H_
