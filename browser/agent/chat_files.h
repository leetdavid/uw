// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_FILES_H_
#define UW_BROWSER_AGENT_CHAT_FILES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "uw/components/agent/attachment.h"

// The files a chat's messages carry: copies in the chat's folder, and what
// the model reads of each. Every function but CopyNamed() blocks. See
// docs/adr/0020-attached-files-are-copies-the-agent-reaches-by-path.md.
namespace uw {

// Past these the model gets the start of a file and its path.
inline constexpr size_t kMaxTextCharacters = 100000;
inline constexpr int64_t kMaxPdfBytes = 32 * 1024 * 1024;
inline constexpr int kMaxPdfPages = 100;

// A new, empty copy in `folder` for a file named `name`: the name's last
// part, numbered when the folder already holds one by that name. Empty when
// it cannot be made.
base::FilePath StartCopy(const base::FilePath& folder, std::string_view name);
// Where the copy `saved` is in `folder`, or empty when `saved` is not a name
// StartCopy() gives.
base::FilePath CopyNamed(const base::FilePath& folder, std::string_view saved);
// Copies the file at `from` into `folder`, named as StartCopy() names it.
// Empty when it fails.
base::FilePath CopyInto(const base::FilePath& folder,
                        const base::FilePath& from);

// The files of a message as the model gets them, and what the chat shows the
// model missed of each, in the same order.
struct ReadFiles {
  ReadFiles();
  ReadFiles(ReadFiles&&);
  ReadFiles& operator=(ReadFiles&&);
  ~ReadFiles();

  std::vector<Attachment> files;
  std::vector<std::string> missed;
};

// Reads what the model gets of each of `files` from its copy: the start of a
// text file, and a PDF whole when it is small enough, beside the text the
// page read of it. A model that `reads_pdfs` gets that PDF in place of its
// text. Any other file goes by its path alone.
ReadFiles ReadForModel(std::vector<Attachment> files, bool reads_pdfs);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_FILES_H_
