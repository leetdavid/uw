// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/problem_reports.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/common/chrome_paths.h"
#include "components/crash/core/app/crashpad.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"
#include "uw/browser/branding/release_version.h"

namespace uw {
namespace {

// More than enough for real problems; a page stuck in a loop stops here.
constexpr size_t kMaxReports = 100;
constexpr size_t kMaxText = 500;
constexpr size_t kMaxFrames = 30;
constexpr size_t kMaxAnswer = 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_problem_reports", R"(
      semantics {
        sender: "uw problem reports"
        description:
          "Sends a problem on one of uw's own pages, such as the chat page or "
          "settings, to PostHog so it can be fixed: a rage click, a click "
          "after which nothing changed, or an error the page did not catch."
        trigger:
          "A problem on one of uw's own pages, in a release, after the user "
          "said Yes to \"Help fix uw?\"."
        data:
          "The page's address; the clicked control's tag, ARIA role, id, and "
          "label, or the error's type, message, and stack; uw's and "
          "Chromium's versions; and a random install ID. Never what a page "
          "shows or what the user typed, and nothing from websites."
        destination: OTHER
        destination_other: "PostHog"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings, Privacy and security, Send crash and problem reports."
        policy_exception_justification: "Not implemented."
      })");

// The page's address without its query, and with any ID in its path, such as
// a chat's, as :id, so PostHog counts problems by page.
std::string PageAddress(const GURL& page) {
  GURL::Replacements address_only;
  address_only.ClearUsername();
  address_only.ClearPassword();
  address_only.ClearQuery();
  address_only.ClearRef();
  std::string url = page.ReplaceComponents(address_only).spec();
  RE2::GlobalReplace(
      &url, R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})",
      ":id");
  return url;
}

std::string Short(const std::string& text) {
  return text.substr(0, kMaxText);
}

const char* EventName(mojom::ProblemKind kind) {
  switch (kind) {
    case mojom::ProblemKind::kRageClick:
      return "$rageclick";
    case mojom::ProblemKind::kDeadClick:
      return "$dead_click";
    case mojom::ProblemKind::kError:
      return "$exception";
  }
}

// An error as PostHog's error tracking reads it.
base::ListValue ExceptionList(const mojom::PageProblem& problem) {
  base::ListValue frames;
  for (size_t i = 0; i < problem.stack.size() && i < kMaxFrames; ++i) {
    const mojom::StackFrame& frame = *problem.stack[i];
    frames.Append(base::DictValue()
                      .Set("function", Short(frame.function))
                      .Set("filename", Short(frame.file))
                      .Set("lineno", static_cast<int>(frame.line))
                      .Set("colno", static_cast<int>(frame.column))
                      .Set("platform", "web:javascript")
                      .Set("in_app", true));
  }
  return base::ListValue().Append(
      base::DictValue()
          .Set("type", Short(problem.error_type))
          .Set("value", Short(problem.message))
          .Set("mechanism",
               base::DictValue().Set("handled", false).Set("synthetic", false))
          .Set("stacktrace", base::DictValue()
                                 .Set("type", "raw")
                                 .Set("frames", std::move(frames))));
}

}  // namespace

// static
ProblemReports& ProblemReports::Get() {
  static base::NoDestructor<ProblemReports> reports;
  return *reports;
}

ProblemReports::ProblemReports()
    : files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  base::FilePath crash_dumps;
  base::PathService::Get(chrome::DIR_CRASH_DUMPS, &crash_dumps);
  keys_ = GetReleaseKeys();
  file_ = ProblemReportsAnswerFile(crash_dumps);
  if (keys_.available()) {
    files_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&ReadProblemReportsAnswer, file_),
        base::BindOnce(&ProblemReports::OnLoaded, base::Unretained(this)));
  }
}

ProblemReports::~ProblemReports() = default;

void ProblemReports::OnLoaded(std::optional<ProblemReportsAnswer> answer) {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  answer_ = std::move(answer);
  observers_.Notify();
}

void ProblemReports::Answer(bool send) {
  ProblemReportsAnswer answer{.send = send};
  if (send) {
    answer.install_id = answer_ && !answer_->install_id.empty()
                            ? answer_->install_id
                            : base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  answer_ = answer;
  loaded_ = true;
  files_->PostTask(FROM_HERE,
                   base::BindOnce(
                       [](base::FilePath file, ProblemReportsAnswer answer,
                          bool uploads) {
                         WriteProblemReportsAnswer(file, answer);
                         crash_reporter::SetUploadConsent(uploads);
                       },
                       file_, answer, sending()));
  observers_.Notify();
}

base::CallbackListSubscription ProblemReports::ObserveAnswer(
    base::RepeatingClosure callback) {
  return observers_.Add(std::move(callback));
}

void ProblemReports::ReportPageProblem(const GURL& page,
                                       const mojom::PageProblem& problem) {
  if (!sending() || keys_.posthog_key.empty() || sent_ >= kMaxReports) {
    return;
  }
  const std::string url = PageAddress(page);
  if (!seen_
           .insert(base::StrCat({EventName(problem.kind), url, problem.tag,
                                 problem.role, problem.element_id,
                                 problem.label, problem.error_type,
                                 problem.message}))
           .second) {
    return;
  }
  ++sent_;

  base::DictValue properties;
  properties.Set("$current_url", url);
  properties.Set("$lib", "uw");
  properties.Set("$process_person_profile", false);
  properties.Set("uw_version", ReleaseVersion().value_or(std::string()));
  properties.Set("chromium_version", version_info::GetVersionNumber());
  if (problem.kind == mojom::ProblemKind::kError) {
    properties.Set("$exception_list", ExceptionList(problem));
  } else {
    properties.Set("element_tag", Short(problem.tag));
    properties.Set("element_role", Short(problem.role));
    properties.Set("element_id", Short(problem.element_id));
    properties.Set("element_label", Short(problem.label));
  }
  std::optional<std::string> body =
      base::WriteJson(base::DictValue()
                          .Set("api_key", keys_.posthog_key)
                          .Set("event", EventName(problem.kind))
                          .Set("distinct_id", answer_->install_id)
                          .Set("properties", std::move(properties)));
  if (!body) {
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(keys_.posthog_host).Resolve("/i/v0/e/");
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  loaders_.push_front(
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation));
  auto loader = loaders_.begin();
  (*loader)->AttachStringForUpload(*body, "application/json");
  (*loader)->DownloadToString(
      g_browser_process->shared_url_loader_factory().get(),
      base::BindOnce(&ProblemReports::OnSent, base::Unretained(this), loader),
      kMaxAnswer);
}

void ProblemReports::OnSent(
    std::list<std::unique_ptr<network::SimpleURLLoader>>::iterator loader,
    std::optional<std::string> body) {
  loaders_.erase(loader);
  if (sent_for_testing_) {
    sent_for_testing_.Run();
  }
}

void ProblemReports::SetUpForTesting(ReleaseKeys keys,
                                     base::FilePath answer_file) {
  sent_ = 0;
  seen_.clear();
  keys_ = std::move(keys);
  file_ = std::move(answer_file);
  base::ScopedAllowBlockingForTesting allow_blocking;
  answer_ = ReadProblemReportsAnswer(file_);
  loaded_ = true;
  observers_.Notify();
}

void ProblemReports::SetSentCallbackForTesting(
    base::RepeatingClosure callback) {
  sent_for_testing_ = std::move(callback);
}

void ProblemReports::FlushForTesting() {
  base::RunLoop run_loop;
  files_->PostTask(FROM_HERE, run_loop.QuitClosure());
  run_loop.Run();
}

}  // namespace uw
