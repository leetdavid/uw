// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTS_H_
#define UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTS_H_

#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "uw/browser/problem_reports/answer_file.h"
#include "uw/browser/problem_reports/problem_reporter.mojom.h"
#include "uw/browser/problem_reports/release_keys.h"

class GURL;

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace uw {

// Whether uw sends crash and problem reports, and the sending of problems on
// uw's own pages to PostHog. Only a release can send, and only after the user
// said Yes. See docs/plans/distribution.md, "Problem reports".
class ProblemReports {
 public:
  static ProblemReports& Get();

  ProblemReports(const ProblemReports&) = delete;
  ProblemReports& operator=(const ProblemReports&) = delete;

  // True in a release, which names the addresses reports go to.
  bool available() const { return keys_.available(); }
  // True in a release until the user answers "Help fix uw?". False until the
  // answer file is read, early in each launch.
  bool ShouldAsk() const { return available() && loaded_ && !answer_; }
  bool sending() const { return available() && answer_ && answer_->send; }

  // Saves the answer, turns Crashpad's uploads on or off, and makes or deletes
  // the install ID.
  void Answer(bool send);
  base::CallbackListSubscription ObserveAnswer(
      base::RepeatingClosure callback);

  // Sends a problem on the uw page at `page` to PostHog, if the user said Yes.
  void ReportPageProblem(const GURL& page, const mojom::PageProblem& problem);

  // Tests use their own release keys and answer file, and wait for each
  // report to reach PostHog's stand-in.
  void SetUpForTesting(ReleaseKeys keys, base::FilePath answer_file);
  void SetSentCallbackForTesting(base::RepeatingClosure callback);
  // Waits until the answer file is written.
  void FlushForTesting();

 private:
  friend class base::NoDestructor<ProblemReports>;

  ProblemReports();
  ~ProblemReports();

  void OnLoaded(std::optional<ProblemReportsAnswer> answer);
  void OnSent(std::list<std::unique_ptr<network::SimpleURLLoader>>::iterator
                  loader,
              std::optional<std::string> body);

  ReleaseKeys keys_;
  base::FilePath file_;
  std::optional<ProblemReportsAnswer> answer_;
  // The answer file has been read, or the user answered first.
  bool loaded_ = false;
  scoped_refptr<base::SequencedTaskRunner> files_;
  base::RepeatingClosureList observers_;
  std::list<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  // Reports sent this launch, and what each was about, so a page stuck in a
  // loop sends each problem once and never floods PostHog.
  size_t sent_ = 0;
  std::set<std::string> seen_;
  base::RepeatingClosure sent_for_testing_;
};

}  // namespace uw

#endif  // UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTS_H_
