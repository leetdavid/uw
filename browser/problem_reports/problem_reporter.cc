// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/problem_reporter.h"

#include <utility>

#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "uw/browser/problem_reports/problem_reports.h"

namespace uw {

ProblemReporter::ProblemReporter(
    content::WebUI* web_ui,
    mojo::PendingReceiver<mojom::ProblemReporter> receiver)
    : web_ui_(web_ui), receiver_(this, std::move(receiver)) {}

ProblemReporter::~ProblemReporter() = default;

void ProblemReporter::Report(mojom::PageProblemPtr problem) {
  ProblemReports::Get().ReportPageProblem(
      web_ui_->GetWebContents()->GetLastCommittedURL(), *problem);
}

}  // namespace uw
