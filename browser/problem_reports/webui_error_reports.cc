// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/webui_error_reports.h"

#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "components/crash/content/browser/error_reporting/javascript_error_report.h"
#include "components/crash/content/browser/error_reporting/js_error_report_processor.h"
#include "components/feedback/redaction_tool/redaction_tool.h"
#include "content/public/browser/webui_config.h"
#include "content/public/browser/webui_config_map.h"
#include "url/gurl.h"
#include "uw/browser/problem_reports/release_keys.h"

namespace uw {
namespace {

// Chromium's switch, which each WebUI page reads as it loads.
constexpr char kDisableCrashOnWebUIJsError[] =
    "disable-crash-on-webui-js-error";
// As Chrome cuts a message it sends to Google.
constexpr size_t kMaxLength = 1000;

base::RepeatingCallback<void(const WebUIErrorReport&)>& ReportCallback() {
  static base::NoDestructor<
      base::RepeatingCallback<void(const WebUIErrorReport&)>>
      callback;
  return *callback;
}

std::string Cut(const std::string& text) {
  std::string cut;
  base::TruncateUTF8ToByteSize(text, kMaxLength, &cut);
  return cut;
}

// Runs off the UI thread, since Chrome's redaction is slow. The stack keeps
// only its frames: V8 starts it with the message, unredacted.
WebUIErrorReport Redact(JavaScriptErrorReport error) {
  WebUIErrorReport report;
  report.page = error.page_url.value_or(error.url);
  report.source = error.line_number
                      ? base::StrCat({error.url, ":",
                                      base::NumberToString(*error.line_number)})
                      : error.url;
  report.message = Cut(redaction::RedactionTool().Redact(error.message));
  const std::string stack = error.stack_trace.value_or("");
  std::vector<std::string_view> frames;
  for (std::string_view line : base::SplitStringPiece(
           stack, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with("at ")) {
      frames.push_back(line);
    }
  }
  report.stack = Cut(base::JoinString(frames, "\n"));
  return report;
}

// Crashpad uploads the dump to Sentry after a Yes, at most one a day, as
// Chromium limits dumps from one place.
void Send(base::OnceClosure done, WebUIErrorReport report) {
  SCOPED_CRASH_KEY_STRING256("uw-webui-error", "page", report.page);
  SCOPED_CRASH_KEY_STRING256("uw-webui-error", "source", report.source);
  SCOPED_CRASH_KEY_STRING1024("uw-webui-error", "message", report.message);
  SCOPED_CRASH_KEY_STRING1024("uw-webui-error", "stack", report.stack);
  base::debug::DumpWithoutCrashing();
  if (ReportCallback()) {
    ReportCallback().Run(report);
  }
  std::move(done).Run();
}

// In place of Chromium's error reporter, which only Google Chrome builds have
// and which sends these errors to Google.
class WebUIErrorReporter : public JsErrorReportProcessor {
 public:
  static void Install() { SetDefault(base::AdoptRef(new WebUIErrorReporter)); }

  void SendErrorReport(JavaScriptErrorReport error,
                       base::OnceClosure done,
                       content::BrowserContext* browser_context) override {
    content::WebUIConfig* config =
        content::WebUIConfigMap::GetInstance().GetConfig(
            browser_context, GURL(error.page_url.value_or(error.url)));
    if (!config || !config->ShouldCrashOnJavascriptErrorInDevelopmentBuild()) {
      std::move(done).Run();
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&Redact, std::move(error)),
        base::BindOnce(&Send, std::move(done)));
  }

 private:
  ~WebUIErrorReporter() override = default;
};

}  // namespace

void SetUpWebUIErrorReports() {
  WebUIErrorReporter::Install();
  if (GetReleaseKeys().available()) {
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        kDisableCrashOnWebUIJsError);
  }
}

void SetWebUIErrorReportCallbackForTesting(
    base::RepeatingCallback<void(const WebUIErrorReport&)> callback) {
  ReportCallback() = std::move(callback);
}

}  // namespace uw
