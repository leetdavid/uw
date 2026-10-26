// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_files.h"

#include <utility>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace uw {
namespace {

bool IsPdf(const Attachment& file) {
  return file.type == "application/pdf" ||
         base::EndsWith(file.name, ".pdf",
                        base::CompareCase::INSENSITIVE_ASCII);
}

// Cuts UTF-8 `text` to kMaxTextCharacters characters, and says whether it
// was longer.
bool Cut(std::string& text) {
  size_t characters = 0;
  for (size_t at = 0; at < text.size(); ++at) {
    if ((static_cast<unsigned char>(text[at]) & 0xC0) != 0x80 &&
        ++characters > kMaxTextCharacters) {
      text.resize(at);
      return true;
    }
  }
  return false;
}

// The text the page read of a PDF, and the PDF itself when it is small
// enough to go whole.
std::string ReadPdf(Attachment& file, bool reads_pdfs) {
  const bool cut = Cut(file.text);
  const bool first_pages = file.pages > kMaxPdfPages;
  if (!file.text.empty() && (cut || first_pages)) {
    base::StrAppend(&file.text,
                    {"\n[", cut ? "The first 100,000 characters of its text"
                                : "The text of its first 100 pages",
                     ". Read the rest from its path.]"});
  }
  std::string data;
  if (file.pages > 0 && !first_pages && file.size <= kMaxPdfBytes &&
      base::ReadFileToStringWithMaxSize(base::FilePath::FromUTF8Unsafe(file.path),
                                        &data, kMaxPdfBytes)) {
    file.pdf =
        base::StrCat({"data:application/pdf;base64,", base::Base64Encode(data)});
  }
  if (reads_pdfs && !file.pdf.empty()) {
    return std::string();
  }
  if (file.text.empty()) {
    return "no text";
  }
  if (cut) {
    return "read in part";
  }
  return first_pages ? "first 100 pages read" : std::string();
}

// The start of a file that is UTF-8 text. Nothing for any other file.
std::string ReadText(Attachment& file) {
  // A character takes at most 4 bytes, so this holds the first
  // kMaxTextCharacters characters.
  std::string start;
  const bool whole = base::ReadFileToStringWithMaxSize(
      base::FilePath::FromUTF8Unsafe(file.path), &start,
      kMaxTextCharacters * 4);
  if (!whole) {
    // The read may end inside a character.
    base::TruncateUTF8ToByteSize(start, start.size(), &start);
  }
  if (start.empty() || start.find('\0') != std::string::npos ||
      !base::IsStringUTF8AllowingNoncharacters(start)) {
    return std::string();
  }
  const bool cut = Cut(start) || !whole;
  file.text = std::move(start);
  if (!cut) {
    return std::string();
  }
  base::StrAppend(&file.text, {"\n[The first 100,000 characters. Read the "
                               "rest from its path.]"});
  return "read in part";
}

}  // namespace

ReadFiles::ReadFiles() = default;
ReadFiles::ReadFiles(ReadFiles&&) = default;
ReadFiles& ReadFiles::operator=(ReadFiles&&) = default;
ReadFiles::~ReadFiles() = default;

base::FilePath StartCopy(const base::FilePath& folder, std::string_view name) {
  base::FilePath base = base::FilePath::FromUTF8Unsafe(name).BaseName();
  if (base.empty() || base.value() == base::FilePath::kCurrentDirectory ||
      base.value() == base::FilePath::kParentDirectory ||
      base.value() == "/") {
    base = base::FilePath("file");
  }
  if (!base::CreateDirectory(folder)) {
    return base::FilePath();
  }
  const base::FilePath path = base::GetUniquePath(folder.Append(base));
  if (path.empty() || !base::WriteFile(path, "")) {
    return base::FilePath();
  }
  return path;
}

base::FilePath CopyNamed(const base::FilePath& folder, std::string_view saved) {
  const base::FilePath name = base::FilePath::FromUTF8Unsafe(saved);
  if (saved.empty() || name.BaseName() != name ||
      name.value() == base::FilePath::kCurrentDirectory ||
      name.value() == base::FilePath::kParentDirectory) {
    return base::FilePath();
  }
  return folder.Append(name);
}

base::FilePath CopyInto(const base::FilePath& folder,
                        const base::FilePath& from) {
  const base::FilePath to = StartCopy(folder, from.BaseName().AsUTF8Unsafe());
  if (to.empty()) {
    return base::FilePath();
  }
  if (!base::CopyFile(from, to)) {
    base::DeleteFile(to);
    return base::FilePath();
  }
  return to;
}

ReadFiles ReadForModel(std::vector<Attachment> files, bool reads_pdfs) {
  ReadFiles read;
  for (Attachment& file : files) {
    if (!file.image.empty() || file.path.empty()) {
      read.missed.emplace_back();
    } else if (IsPdf(file)) {
      read.missed.push_back(ReadPdf(file, reads_pdfs));
    } else {
      file.text.clear();
      read.missed.push_back(ReadText(file));
    }
  }
  read.files = std::move(files);
  return read;
}

}  // namespace uw
