// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/skills.h"

#include <algorithm>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace uw {
namespace {

constexpr char kSkillFile[] = "SKILL.md";
constexpr char kMadeInChat[] = "uw-made-in: chat";
// A SKILL.md larger than this is not read.
constexpr int64_t kMaxSkillFile = 256 * 1024;

// The lines between the "---" lines that open a SKILL.md, and where the
// instructions after them start.
struct FrontMatter {
  std::vector<std::string_view> lines;
  size_t body = 0;
};

std::optional<FrontMatter> SplitFrontMatter(std::string_view text) {
  size_t at = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
  // The line that starts at `at`, and where the next one starts.
  const auto line_at = [&](size_t& next) {
    const size_t newline = text.find('\n', at);
    next = newline == std::string_view::npos ? text.size() : newline + 1;
    return base::TrimWhitespaceASCII(
        text.substr(
            at,
            (newline == std::string_view::npos ? text.size() : newline) - at),
        base::TRIM_TRAILING);
  };
  size_t next = 0;
  if (base::TrimWhitespaceASCII(line_at(next), base::TRIM_ALL) != "---") {
    return std::nullopt;
  }
  FrontMatter front;
  for (at = next; at < text.size(); at = next) {
    const std::string_view line = line_at(next);
    if (line == "---") {
      front.body = next;
      return front;
    }
    front.lines.push_back(line);
  }
  return std::nullopt;
}

// A YAML scalar as uw meets it in front matter: plain, or in single or
// double quotes.
std::string Unquote(std::string_view value) {
  value = base::TrimWhitespaceASCII(value, base::TRIM_ALL);
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    std::string text(value.substr(1, value.size() - 2));
    base::ReplaceSubstringsAfterOffset(&text, 0, "''", "'");
    return text;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    std::string text(value.substr(1, value.size() - 2));
    base::ReplaceSubstringsAfterOffset(&text, 0, "\\\"", "\"");
    base::ReplaceSubstringsAfterOffset(&text, 0, "\\\\", "\\");
    return text;
  }
  return std::string(value);
}

}  // namespace

std::optional<SkillFrontMatter> ReadSkillFrontMatter(std::string_view text) {
  std::optional<FrontMatter> split = SplitFrontMatter(text);
  if (!split) {
    return std::nullopt;
  }
  SkillFrontMatter front;
  std::string* value = nullptr;
  for (std::string_view line : split->lines) {
    if (base::TrimWhitespaceASCII(line, base::TRIM_ALL) == kMadeInChat) {
      front.made_in_chat = true;
    }
    const bool continues = !line.empty() && (line[0] == ' ' || line[0] == '\t');
    if (continues) {
      // A folded or multi-line value goes on in indented lines.
      if (value) {
        const std::string_view more =
            base::TrimWhitespaceASCII(line, base::TRIM_ALL);
        if (!more.empty()) {
          base::StrAppend(value, {value->empty() ? "" : " ", more});
        }
      }
      continue;
    }
    value = nullptr;
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    const std::string_view key =
        base::TrimWhitespaceASCII(line.substr(0, colon), base::TRIM_ALL);
    if (key == "disable-model-invocation") {
      front.for_agent = !base::EqualsCaseInsensitiveASCII(
          Unquote(line.substr(colon + 1)), "true");
      continue;
    }
    if (key == "user-invocable") {
      front.for_user = !base::EqualsCaseInsensitiveASCII(
          Unquote(line.substr(colon + 1)), "false");
      continue;
    }
    if (key == "name") {
      value = &front.name;
    } else if (key == "description") {
      value = &front.description;
    } else if (key == "argument-hint") {
      value = &front.argument_hint;
    } else {
      continue;
    }
    const std::string_view rest =
        base::TrimWhitespaceASCII(line.substr(colon + 1), base::TRIM_ALL);
    *value = rest == ">" || rest == ">-" || rest == "|" || rest == "|-"
                 ? std::string()
                 : Unquote(rest);
  }
  if (front.name.empty() || front.description.empty()) {
    return std::nullopt;
  }
  return front;
}

std::string_view SkillInstructions(std::string_view text) {
  std::optional<FrontMatter> split = SplitFrontMatter(text);
  return base::TrimWhitespaceASCII(split ? text.substr(split->body) : text,
                                   base::TRIM_ALL);
}

std::string StartedSkillMessage(std::string_view instructions,
                                std::string_view arguments) {
  std::string message(instructions);
  if (message.find("$ARGUMENTS") != std::string::npos) {
    base::ReplaceSubstringsAfterOffset(&message, 0, "$ARGUMENTS", arguments);
  } else if (!arguments.empty()) {
    base::StrAppend(&message, {"\n\n", arguments});
  }
  return message;
}

bool IsSkillName(std::string_view name) {
  return !name.empty() && name.size() <= 64 &&
         std::ranges::all_of(name, [](char c) {
           return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-';
         });
}

std::vector<Skill> FindSkills(const base::FilePath& folder,
                              bool agents_folder) {
  std::vector<Skill> skills;
  base::FileEnumerator folders(folder, /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = folders.Next(); !path.empty();
       path = folders.Next()) {
    const base::FilePath file = path.AppendASCII(kSkillFile);
    std::optional<int64_t> size = base::GetFileSize(file);
    std::string text;
    if (!size || *size > kMaxSkillFile ||
        !base::ReadFileToString(file, &text)) {
      continue;
    }
    std::optional<SkillFrontMatter> front = ReadSkillFrontMatter(text);
    if (!front) {
      continue;
    }
    Skill skill;
    skill.name = std::move(front->name);
    skill.description = std::move(front->description);
    skill.file = file;
    skill.source = agents_folder         ? Skill::Source::kAgents
                   : front->made_in_chat ? Skill::Source::kChat
                                         : Skill::Source::kUw;
    skill.for_agent = front->for_agent;
    skill.for_user = front->for_user;
    skill.argument_hint = std::move(front->argument_hint);
    skills.push_back(std::move(skill));
  }
  std::ranges::sort(skills, {}, &Skill::name);
  return skills;
}

bool WriteSkill(const base::FilePath& folder,
                std::string_view name,
                std::string_view description,
                std::string_view instructions) {
  const base::FilePath skill = folder.AppendASCII(name);
  if (!IsSkillName(name) || base::PathExists(skill) ||
      !base::CreateDirectory(skill)) {
    return false;
  }
  // One line in double quotes, escaped the way ReadSkillFrontMatter reads.
  std::string quoted(description);
  base::ReplaceChars(quoted, "\r\n", " ", &quoted);
  base::ReplaceSubstringsAfterOffset(&quoted, 0, "\\", "\\\\");
  base::ReplaceSubstringsAfterOffset(&quoted, 0, "\"", "\\\"");
  const std::string text = base::StrCat(
      {"---\nname: ", name, "\ndescription: \"", quoted, "\"\nmetadata:\n  ",
       kMadeInChat, "\n---\n\n",
       base::TrimWhitespaceASCII(instructions, base::TRIM_ALL), "\n"});
  return base::WriteFile(skill.AppendASCII(kSkillFile), text);
}

}  // namespace uw
