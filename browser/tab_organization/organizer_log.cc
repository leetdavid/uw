// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/organizer_log.h"

#include <optional>
#include <utility>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "net/base/net_errors.h"

namespace uw {
namespace {

// Keeps JSON as JSON, so a line reads as one nested object.
base::Value Parsed(const std::string& text) {
  std::optional<base::Value> value =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  return value ? std::move(*value) : base::Value(text);
}

void AppendLine(const base::FilePath& path, const std::string& line) {
  base::File file(path, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  if (file.IsValid()) {
    file.WriteAtCurrentPos(base::as_byte_span(line));
  }
}

// One runner keeps the lines in the order the answers arrived.
base::SequencedTaskRunner& Writer() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return **runner;
}

}  // namespace

OrganizerExchange::OrganizerExchange() = default;
OrganizerExchange::OrganizerExchange(const OrganizerExchange&) = default;
OrganizerExchange& OrganizerExchange::operator=(const OrganizerExchange&) =
    default;
OrganizerExchange::OrganizerExchange(OrganizerExchange&&) = default;
OrganizerExchange& OrganizerExchange::operator=(OrganizerExchange&&) = default;
OrganizerExchange::~OrganizerExchange() = default;

void LogOrganizerExchange(std::string_view purpose,
                          const OrganizerExchange& exchange,
                          std::string_view result,
                          base::DictValue details) {
  const base::FilePath path =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          kOrganizerLogSwitch);
  if (path.empty()) {
    return;
  }
  base::DictValue entry = std::move(details);
  entry.Set("time", base::TimeFormatAsIso8601(base::Time::Now()));
  entry.Set("purpose", purpose);
  entry.Set("model", exchange.model);
  entry.Set("endpoint", exchange.url.spec());
  entry.Set("took_ms", static_cast<int>(exchange.took.InMilliseconds()));
  entry.Set("http_status", exchange.answer.http_status);
  entry.Set("error", exchange.answer.net_error == net::OK
                         ? std::string()
                         : net::ErrorToShortString(exchange.answer.net_error));
  entry.Set("result", result);
  entry.Set("request", Parsed(exchange.body));
  entry.Set("answer", Parsed(exchange.answer.body));
  Writer().PostTask(
      FROM_HERE, base::BindOnce(&AppendLine, path,
                                base::WriteJson(entry).value_or("{}") + "\n"));
}

}  // namespace uw
