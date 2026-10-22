// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_run.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "uw/browser/agent/run_window.h"

namespace uw {

namespace {

AgentLoop::Step CopyOf(const AgentLoop::Step& step) {
  AgentLoop::Step copy;
  copy.kind = step.kind;
  copy.number = step.number;
  copy.tool = step.tool;
  copy.args = step.args.Clone();
  copy.result = step.result.Clone();
  copy.text = step.text;
  copy.model_unreachable = step.model_unreachable;
  return copy;
}

}  // namespace

ChatRun::ChatRun(Profile* profile,
                 AgentModel model,
                 ApprovalMode mode,
                 std::string_view brief,
                 Done done)
    : done_(std::move(done)) {
  tabs::TabInterface* run_tab = OpenRunTab(profile);
  if (!run_tab) {
    Outcome outcome;
    outcome.error = "The run's window could not open.";
    End(std::move(outcome));
    return;
  }
  run_tab_ = run_tab->GetHandle();
  loop_ = std::make_unique<AgentLoop>(
      *run_tab, std::move(model),
      base::BindRepeating(&ChatRun::OnStep, weak_factory_.GetWeakPtr()));
  loop_->SetMode(mode);
  // The loop is this run's own, so it never calls after the run ends.
  loop_->SetReport(
      base::BindRepeating(&ChatRun::OnReport, base::Unretained(this)));
  loop_->Send(brief);
}

ChatRun::~ChatRun() {
  loop_.reset();
  if (tabs::TabInterface* run_tab = run_tab_.Get()) {
    CloseRunTabs(*run_tab);
  }
}

void ChatRun::SetApprover(AgentLoop::Approve approve) {
  if (loop_) {
    loop_->SetApprover(std::move(approve));
  }
}

void ChatRun::Stop() {
  if (loop_) {
    loop_->Stop();
  }
}

base::DictValue ChatRun::OnReport(const base::DictValue& args) {
  base::expected<RunReport, std::string> report = ParseReport(args);
  if (!report.has_value()) {
    return base::DictValue().Set("error", std::move(report.error()));
  }
  report_ = std::move(*report);
  return base::DictValue().Set("saved", true);
}

void ChatRun::OnStep(const AgentLoop::Step& step) {
  steps_.push_back(CopyOf(step));
  if (step.kind == AgentLoop::Step::Kind::kTool) {
    return;
  }
  Outcome outcome;
  if (report_) {
    outcome.kind = Outcome::Kind::kReported;
    outcome.report = std::move(*report_);
  } else if (step.kind == AgentLoop::Step::Kind::kStoppedByUser) {
    outcome.kind = Outcome::Kind::kStopped;
  } else if (step.model_unreachable) {
    outcome.kind = Outcome::Kind::kModelUnreachable;
    outcome.error = step.text;
  } else {
    outcome.kind = Outcome::Kind::kFailed;
    outcome.error = step.kind == AgentLoop::Step::Kind::kFailed
                        ? step.text
                        : "The run ended without a report.";
  }
  End(std::move(outcome));
}

void ChatRun::End(Outcome outcome) {
  outcome.steps = std::move(steps_);
  // The loop that called OnStep may still be on the stack, and `done` may
  // destroy this run.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(done_), std::move(outcome)));
}

}  // namespace uw
