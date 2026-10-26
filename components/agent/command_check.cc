// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/command_check.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/agent/check_questions.h"

namespace uw {
namespace {

// Auto runs a step without asking only when it is at least this likely safe.
constexpr double kSafeBar = 0.8;
// Every mode asks before a step at least this likely to read a sensitive
// file. Jev gives a build or a dev server up to 0.36, as such programs load
// .env files themselves.
constexpr double kSensitiveBar = 0.5;
// How much of the chat the check sees, and of each of its lines.
constexpr size_t kMaxChatLength = 8000;
constexpr size_t kMaxLineLength = 1000;
// How many words of a step the file question offers.
constexpr size_t kMaxWords = 40;
constexpr size_t kMaxWordLength = 200;
constexpr size_t kMaxReasonLength = 200;
// Some chat models think before they answer, and the provider counts that
// thinking against the limit.
constexpr int kMaxChatAnswerTokens = 4096;

constexpr char kDangerQuestion[] = "danger";
constexpr char kFileQuestion[] = "file";
constexpr char kSafe[] = "safe";
constexpr char kOther[] = "other";
constexpr char kNone[] = "none";

constexpr char kDangerInstructions[] =
    "An AI agent wants to take this step in a terminal on the user's Mac. "
    "Decide what it could do, given what the user asked.";
constexpr char kFileInstructions[] =
    "Decide which sensitive file this step may read, if any. A sensitive "
    "file holds secrets: a .env file, a key or certificate, a password store, "
    "or the credentials of a tool such as ssh, AWS, or gcloud.";
constexpr char kOtherDescription[] =
    "A sensitive file the step does not name, such as one a wildcard or a "
    "search reaches.";
constexpr char kNoneDescription[] =
    "No sensitive file. A program that loads its own settings does not "
    "count.";

CheckOptions DangerOptions() {
  return {
      {kSafe,
       "Nothing hard to undo: it only reads, lists, searches, builds, tests, "
       "or runs code, or makes a small change the user asked for, such as a "
       "new folder or a commit."},
      {"deletes",
       "It deletes or overwrites files or data, such as rm, a > redirect, git "
       "reset --hard, git clean, or git checkout over changes."},
      {"sends",
       "It sends something off this Mac or installs software, such as git "
       "push, an upload, an email or a message, a brew or npm install, or "
       "running a downloaded script."},
      {"system",
       "It changes the system or other apps, such as sudo, system settings, "
       "killing processes, launchctl, or osascript or Shortcuts acting in "
       "other apps."},
      {"secrets", "It prints, copies, or sends passwords, keys, or tokens."},
  };
}

// The words of the step that could name a file, in order and once each.
std::vector<std::string> StepWords(const TerminalStep& step) {
  std::vector<std::string> words;
  for (std::string_view word :
       base::SplitStringPiece(step.command.empty() ? step.text : step.command,
                              " \t\r\n;|&<>()`'\"=,{}", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (word.starts_with('-') || word.size() > kMaxWordLength ||
        std::ranges::none_of(
            word, [](char c) { return base::IsAsciiAlphaNumeric(c); }) ||
        std::ranges::contains(words, word)) {
      continue;
    }
    words.emplace_back(word);
    if (words.size() == kMaxWords) {
      break;
    }
  }
  return words;
}

std::string WordKey(size_t index) {
  return base::StrCat({"w", base::NumberToString(index + 1)});
}

// The word an option of the file question stands for, or nothing.
std::string WordOf(std::string_view key,
                   const std::vector<std::string>& words) {
  size_t number = 0;
  if (!key.starts_with('w') || !base::StringToSizeT(key.substr(1), &number) ||
      number < 1 || number > words.size()) {
    return std::string();
  }
  return words[number - 1];
}

CheckOptions FileOptions(const std::vector<std::string>& words) {
  CheckOptions options;
  for (size_t i = 0; i < words.size(); ++i) {
    options.emplace_back(WordKey(i), words[i]);
  }
  options.emplace_back(kOther, kOtherDescription);
  options.emplace_back(kNone, kNoneDescription);
  return options;
}

std::vector<CheckQuestion> Questions(const CommandCheckInput& input) {
  std::vector<CheckQuestion> questions;
  if (!input.file_only) {
    questions.push_back(
        {kDangerQuestion, kDangerInstructions, DangerOptions()});
  }
  questions.push_back(
      {kFileQuestion,
       base::StrCat({kFileInstructions,
                     " Choose the word of the step that names it, other for "
                     "one it does not name, or none."}),
       FileOptions(StepWords(input.step))});
  return questions;
}

std::string DescribeStep(const TerminalStep& step) {
  const std::string where =
      base::StrCat({"terminal tab ", base::NumberToString(step.tab), ", in ",
                    step.folder.empty() ? "~" : step.folder});
  if (!step.command.empty()) {
    return base::StrCat({"Run in ", where, ":\n", step.command});
  }
  std::vector<std::string> sent;
  if (!step.text.empty()) {
    sent.push_back(base::StrCat({"text \"", step.text, "\""}));
  }
  sent.insert(sent.end(), step.keys.begin(), step.keys.end());
  return base::StrCat({"Keys for the program running in ", where, ":\n",
                       base::JoinString(sent, ", then ")});
}

// The first line, which usually says what the user wants, and the newest
// lines that fit.
std::string DescribeChat(const std::vector<std::string>& chat) {
  std::vector<std::string> lines;
  for (const std::string& line : chat) {
    std::string kept;
    base::TruncateUTF8ToByteSize(line, kMaxLineLength, &kept);
    lines.push_back(std::move(kept));
  }
  size_t length = lines.empty() ? 0 : lines[0].size();
  size_t from = lines.size();
  while (from > 1 && length + lines[from - 1].size() + 1 <= kMaxChatLength) {
    --from;
    length += lines[from].size() + 1;
  }
  std::vector<std::string> kept;
  if (!lines.empty()) {
    kept.push_back(lines[0]);
  }
  if (from > 1) {
    kept.push_back(base::StrCat(
        {"[", base::NumberToString(from - 1), " lines left out]"}));
  }
  kept.insert(kept.end(), lines.begin() + std::max<size_t>(from, 1),
              lines.end());
  return base::JoinString(kept, "\n");
}

std::string DescribeInput(const CommandCheckInput& input) {
  std::string text;
  if (!input.chat.empty()) {
    text =
        base::StrCat({"What the user asked and what the agent did before, "
                      "oldest first:\n",
                      DescribeChat(input.chat), "\n\n"});
  }
  return base::StrCat({text, "The step:\n", DescribeStep(input.step)});
}

std::string ChatInstructions(bool file_only) {
  std::string text = base::StrCat(
      {"You check a step that an AI agent wants to take in a terminal on the "
       "user's Mac, before it runs. The user message says what the user "
       "asked and what the agent did before, then the step. Judge only the "
       "step.\nRespond with only a JSON object in this exact shape:\n",
       file_only ? R"({"file": "<word>", "file_confidence": <number 0..1>})"
                 : R"({"danger": "<option>", "confidence": <number 0..1>, )"
                   R"("reason": "<one short sentence>", "file": "<word>", )"
                   R"("file_confidence": <number 0..1>})",
       "\n"});
  if (!file_only) {
    text += "- \"danger\": what the step could do, one of:";
    for (const auto& [value, description] : DangerOptions()) {
      base::StrAppend(&text, {"\n  - \"", value, "\": ", description});
    }
    text +=
        "\n- \"confidence\": how sure you are of \"danger\".\n"
        "- \"reason\": unless the step is safe, what could go wrong, in one "
        "short sentence for the user.\n";
  }
  base::StrAppend(
      &text,
      {"- \"file\": ", kFileInstructions,
       " Answer with the word of the step that names it, as written there; "
       "\"other\" for one it does not name, such as one a wildcard or a "
       "search reaches; or \"none\". A program that loads its own settings "
       "does not count.\n"
       "- \"file_confidence\": how sure you are of \"file\"."});
  return text;
}

std::optional<std::string> BuildChat(std::string_view model,
                                     const CommandCheckInput& input) {
  return base::WriteJson(
      base::DictValue()
          .Set("model", model)
          .Set("messages",
               base::ListValue()
                   .Append(
                       base::DictValue()
                           .Set("role", "system")
                           .Set("content", ChatInstructions(input.file_only)))
                   .Append(UserMessage(DescribeInput(input))))
          .Set("max_tokens", kMaxChatAnswerTokens));
}

// The answer's choice when it is one of `options` other than `except`, or else
// the one its table finds likeliest. Empty when the table lists no other.
std::string Leading(const base::DictValue& answer,
                    const CheckOptions& options,
                    std::string_view except) {
  const std::string* choice = answer.FindString("choice");
  if (choice && *choice != except && IsCheckOption(options, *choice)) {
    return *choice;
  }
  std::string leading;
  double most = 0.0;
  for (const auto& [option, description] : options) {
    const std::optional<double> probability = ListedProbability(answer, option);
    if (option != except && probability && *probability > most) {
      most = *probability;
      leading = option;
    }
  }
  return leading;
}

std::optional<CommandCheckAnswer> ReadDecision(const base::DictValue* danger,
                                               const base::DictValue* file,
                                               const CommandCheckInput& input) {
  const std::vector<std::string> words = StepWords(input.step);
  const CheckOptions file_options = FileOptions(words);
  if (!ChoseCheckOption(file, file_options)) {
    return std::nullopt;
  }
  const std::optional<double> none = ProbabilityOfOption(*file, kNone);
  if (!none) {
    return std::nullopt;
  }
  CommandCheckAnswer answer;
  answer.sensitive = 1.0 - *none;
  answer.file = WordOf(Leading(*file, file_options, kNone), words);
  if (input.file_only) {
    return answer;
  }
  const CheckOptions danger_options = DangerOptions();
  if (!ChoseCheckOption(danger, danger_options)) {
    return std::nullopt;
  }
  const std::optional<double> safe = ProbabilityOfOption(*danger, kSafe);
  if (!safe) {
    return std::nullopt;
  }
  answer.safe = *safe;
  answer.danger = *danger->FindString("choice");
  return answer;
}

std::optional<CommandCheckAnswer> ReadChat(std::string_view body,
                                           const CommandCheckInput& input) {
  const std::optional<base::DictValue> json = ReadCheckChatAnswer(body);
  if (!json) {
    return std::nullopt;
  }
  const std::string* file = json->FindString("file");
  const std::optional<double> file_confidence =
      json->FindDouble("file_confidence");
  if (!file || !file_confidence || !IsProbability(*file_confidence)) {
    return std::nullopt;
  }
  CommandCheckAnswer answer;
  answer.sensitive = *file == kNone ? 1.0 - *file_confidence : *file_confidence;
  // A model may name ~/.ssh/id_ed25519 as id_ed25519.
  for (const std::string& word : StepWords(input.step)) {
    if (word == *file || word.ends_with(base::StrCat({"/", *file}))) {
      answer.file = word;
      break;
    }
  }
  if (input.file_only) {
    return answer;
  }
  const std::string* danger = json->FindString("danger");
  const std::optional<double> confidence = json->FindDouble("confidence");
  if (!danger || !confidence || !IsProbability(*confidence) ||
      !IsCheckOption(DangerOptions(), *danger)) {
    return std::nullopt;
  }
  answer.danger = *danger;
  answer.safe = *danger == kSafe ? *confidence : 1.0 - *confidence;
  if (const std::string* reason = json->FindString("reason")) {
    answer.reason = OneLine(*reason, kMaxReasonLength);
  }
  return answer;
}

std::string DangerReason(std::string_view danger, std::string_view model) {
  if (danger == "deletes") {
    return "Deletes or overwrites files or data. What it removes may not come "
           "back.";
  }
  if (danger == "sends") {
    return "Sends something off this Mac or installs software. Once sent, it "
           "can't be taken back.";
  }
  if (danger == "system") {
    return "Changes the system or other apps. It reaches beyond this "
           "terminal.";
  }
  if (danger == "secrets") {
    return base::StrCat(
        {"Can show passwords, keys, or tokens. What it prints "
         "goes to ",
         model, "."});
  }
  return "The command check wasn't sure it's safe.";
}

}  // namespace

std::optional<std::string> BuildCommandCheckRequestBody(
    DecisionFormat format,
    std::string_view model,
    const CommandCheckInput& input) {
  if (model.empty() || (input.step.command.empty() && input.step.text.empty() &&
                        input.step.keys.empty())) {
    return std::nullopt;
  }
  if (format == DecisionFormat::kChat) {
    return BuildChat(model, input);
  }
  return BuildCheckQuestionsBody(format, model, DescribeInput(input),
                                 Questions(input));
}

std::optional<CommandCheckAnswer> ParseCommandCheckResponse(
    DecisionFormat format,
    std::string_view body,
    const CommandCheckInput& input) {
  if (format == DecisionFormat::kChat) {
    return ReadChat(body, input);
  }
  const std::optional<base::DictValue> response =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!response) {
    return std::nullopt;
  }
  return ReadDecision(FindCheckAnswer(format, *response, kDangerQuestion),
                      FindCheckAnswer(format, *response, kFileQuestion), input);
}

CommandCheckVerdict JudgeTerminalStep(
    const std::optional<CommandCheckAnswer>& answer,
    bool file_only,
    std::string_view model) {
  CommandCheckVerdict verdict;
  if (!answer) {
    verdict.ask = true;
    verdict.reason = "The command check didn't answer.";
    return verdict;
  }
  if (answer->sensitive >= kSensitiveBar) {
    verdict.ask = true;
    verdict.sensitive = true;
    verdict.file = answer->file;
    verdict.reason =
        base::StrCat({"A sensitive file. It can hold passwords "
                      "and keys, and what the command prints "
                      "goes to ",
                      model, "."});
    return verdict;
  }
  if (file_only || answer->safe >= kSafeBar) {
    return verdict;
  }
  verdict.ask = true;
  verdict.reason = answer->danger != kSafe && !answer->reason.empty()
                       ? answer->reason
                       : DangerReason(answer->danger, model);
  return verdict;
}

}  // namespace uw
