// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_FILE_H_
#define UW_BROWSER_AGENT_CHAT_FILE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"

namespace uw {

// A chat as uw keeps it, in uw_chats/<id>.json in the profile's folder. It is
// rewritten whole after every change, and each write replaces the file in one
// step, so a crash leaves the last whole copy.
class ChatFile {
 public:
  using Loaded = base::OnceCallback<void(std::optional<base::DictValue>)>;

  // A chat's id is a lowercase GUID.
  static bool IsChatId(std::string_view id);
  static std::string NewChatId();
  // Where a profile keeps its chats.
  static base::FilePath FolderFor(const base::FilePath& profile_path);
  // The one sequence that reads and writes chats, so a page that loads a
  // chat again reads it after the writes before.
  static const scoped_refptr<base::SequencedTaskRunner>& Sequence();

  ChatFile(const base::FilePath& profile_path, std::string_view id);
  ChatFile(const ChatFile&) = delete;
  ChatFile& operator=(const ChatFile&) = delete;
  ~ChatFile();

  // Reads the chat. Nothing when it was never saved or cannot be read.
  void Load(Loaded done);
  void Save(const base::DictValue& chat);

 private:
  base::ImportantFileWriter writer_;
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_FILE_H_
