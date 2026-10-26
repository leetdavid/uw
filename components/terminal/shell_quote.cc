// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_quote.h"

#include <string_view>
#include <utility>

#include "base/strings/string_util.h"

namespace uw {
namespace {

// Ghostty's list.
constexpr std::string_view kSpecial = "\\ ()[]{}<>\"'`!#$&;|*?\t";

}  // namespace

std::string QuotePathsForShell(const std::vector<base::FilePath>& paths) {
  std::vector<std::string> words;
  for (const base::FilePath& path : paths) {
    std::string word;
    for (char c : path.value()) {
      if (kSpecial.find(c) != std::string_view::npos) {
        word += '\\';
      }
      word += c;
    }
    words.push_back(std::move(word));
  }
  return base::JoinString(words, " ");
}

}  // namespace uw
