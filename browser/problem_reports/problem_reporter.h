// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTER_H_
#define UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTER_H_

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/problem_reports/problem_reporter.mojom.h"

namespace content {
class WebUI;
}  // namespace content

namespace uw {

// Hears about problems on one of uw's own pages and hands them to
// ProblemReports. Each uw page's WebUI controller keeps one.
class ProblemReporter : public mojom::ProblemReporter {
 public:
  ProblemReporter(content::WebUI* web_ui,
                  mojo::PendingReceiver<mojom::ProblemReporter> receiver);
  ProblemReporter(const ProblemReporter&) = delete;
  ProblemReporter& operator=(const ProblemReporter&) = delete;
  ~ProblemReporter() override;

  // mojom::ProblemReporter:
  void Report(mojom::PageProblemPtr problem) override;

 private:
  const raw_ptr<content::WebUI> web_ui_;
  mojo::Receiver<mojom::ProblemReporter> receiver_;
};

}  // namespace uw

#endif  // UW_BROWSER_PROBLEM_REPORTS_PROBLEM_REPORTER_H_
