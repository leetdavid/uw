// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/answer_file.h"

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace uw {
namespace {

constexpr char kSend[] = "send";
constexpr char kInstallId[] = "install_id";
constexpr size_t kMaxFileSize = 4096;

}  // namespace

base::FilePath ProblemReportsAnswerFile(const base::FilePath& crash_dumps) {
  return crash_dumps.Append(FILE_PATH_LITERAL("uw Problem Reports.json"));
}

std::optional<ProblemReportsAnswer> ReadProblemReportsAnswer(
    const base::FilePath& file) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(file, &text, kMaxFileSize)) {
    return std::nullopt;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  std::optional<bool> send = dict->FindBool(kSend);
  if (!send) {
    return std::nullopt;
  }
  ProblemReportsAnswer answer{.send = *send};
  if (const std::string* id = dict->FindString(kInstallId); id && *send) {
    answer.install_id = *id;
  }
  return answer;
}

bool WriteProblemReportsAnswer(const base::FilePath& file,
                               const ProblemReportsAnswer& answer) {
  base::DictValue dict;
  dict.Set(kSend, answer.send);
  if (answer.send) {
    dict.Set(kInstallId, answer.install_id);
  }
  std::optional<std::string> text = base::WriteJson(dict);
  return text && base::CreateDirectory(file.DirName()) &&
         base::ImportantFileWriter::WriteFileAtomically(file, *text);
}

}  // namespace uw
