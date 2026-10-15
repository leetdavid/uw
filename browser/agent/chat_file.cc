// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_file.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"

namespace uw {
namespace {

// One sequence for every chat, so a page that loads a chat again reads it
// after the writes before.
const scoped_refptr<base::SequencedTaskRunner>& FileSequence() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> sequence(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return *sequence;
}

base::FilePath PathFor(const base::FilePath& profile_path,
                       std::string_view id) {
  return profile_path.AppendASCII("uw_chats")
      .AppendASCII(base::StrCat({id, ".json"}));
}

std::optional<base::DictValue> Read(const base::FilePath& path) {
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
}

}  // namespace

// static
bool ChatFile::IsChatId(std::string_view id) {
  return base::Uuid::ParseLowercase(id).is_valid();
}

// static
std::string ChatFile::NewChatId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

ChatFile::ChatFile(const base::FilePath& profile_path, std::string_view id)
    : writer_(PathFor(profile_path, id), FileSequence()) {
  FileSequence()->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(&base::CreateDirectory),
                                writer_.path().DirName()));
}

ChatFile::~ChatFile() = default;

void ChatFile::Load(Loaded done) {
  FileSequence()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Read, writer_.path()), std::move(done));
}

void ChatFile::Save(const base::DictValue& chat) {
  if (std::optional<std::string> json = base::WriteJson(chat)) {
    writer_.WriteNow(std::move(*json));
  }
}

}  // namespace uw
