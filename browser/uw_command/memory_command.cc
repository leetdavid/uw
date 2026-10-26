// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/memory_store.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/components/agent/memory_tools.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

// Every memory, as uw memory list prints them all.
constexpr size_t kAll = 100000;

void List(CommandRequest request,
          CommandReply reply,
          Profile* profile,
          std::string workspace,
          std::vector<Memory> memories,
          size_t total) {
  std::map<std::string, std::string> titles;
  if (ChatIndex* index = ChatIndex::For(profile)) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      titles[entry.id] = entry.title;
    }
  }
  base::ListValue listed;
  for (const Memory& memory : memories) {
    const auto title = titles.find(memory.chat);
    listed.Append(
        base::DictValue()
            .Set("id", static_cast<double>(memory.id))
            .Set("text", memory.text)
            .Set("chat", memory.chat)
            .Set("chatTitle", title == titles.end() ? "" : title->second)
            .Set("changed", memory.changed.InMillisecondsFSinceUnixEpoch()));
  }
  std::move(reply).Run(CommandResult(
      request.id, base::Value(base::DictValue()
                                  .Set("memories", std::move(listed))
                                  .Set("total", static_cast<double>(total))
                                  .Set("on", profile->GetPrefs()->GetBoolean(
                                                 kAgentMemoryPref))
                                  .Set("workspace", std::move(workspace)))));
}

void Answer(CommandRequest request,
            CommandReply reply,
            Profile* profile,
            std::string workspace) {
  MemoryStore* store = MemoryStore::For(profile);
  if (!store) {
    std::move(reply).Run(CommandError(request.id, kCommandFailed,
                                      "This workspace keeps no memories."));
    return;
  }
  if (request.method == "memory.list") {
    const std::string* search = request.params.FindString("search");
    store->Search(
        MemorySearchWords(search ? *search : ""), kAll,
        base::BindOnce(
            [](CommandRequest request, CommandReply reply, Profile* profile,
               std::string workspace, std::vector<Memory> memories) {
              MemoryStore::For(profile)->Count(base::BindOnce(
                  &List, std::move(request), std::move(reply), profile,
                  std::move(workspace), std::move(memories)));
            },
            std::move(request), std::move(reply), profile,
            std::move(workspace)));
    return;
  }
  if (request.method != "memory.forget") {
    std::move(reply).Run(
        CommandError(request.id, kMethodNotFound,
                     base::StrCat({"This uw can't answer ", request.method,
                                   ". Update uw, then try again."})));
    return;
  }
  const std::optional<double> id = request.params.FindDouble("id");
  if (!id) {
    std::move(reply).Run(CommandError(request.id, kCommandUsage,
                                      "uw memory forget takes a memory's id."));
    return;
  }
  store->Forget(
      static_cast<int64_t>(*id),
      base::BindOnce(
          [](CommandRequest request, CommandReply reply, int64_t id,
             std::optional<Memory> forgot) {
            if (!forgot) {
              std::move(reply).Run(CommandError(
                  request.id, kCommandFailed,
                  base::StrCat({"There is no memory ", base::NumberToString(id),
                                ".\n  uw memory list"})));
              return;
            }
            std::move(reply).Run(CommandResult(
                request.id,
                base::Value(base::DictValue().Set(
                    "forgot", base::DictValue()
                                  .Set("id", static_cast<double>(forgot->id))
                                  .Set("text", forgot->text)))));
          },
          std::move(request), std::move(reply), static_cast<int64_t>(*id)));
}

}  // namespace

void RunMemoryCommand(CommandRequest request, CommandReply reply) {
  base::expected<CommandWorkspace, std::string> chosen =
      ChooseWorkspace(request.params);
  if (!chosen.has_value()) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, chosen.error()));
    return;
  }
  LoadWorkspace(std::move(*chosen),
                base::BindOnce(
                    [](CommandRequest request, CommandReply reply,
                       Profile* profile, const std::string& id) {
                      if (!profile) {
                        std::move(reply).Run(
                            CommandError(request.id, kCommandFailed,
                                         "uw couldn't open that workspace."));
                        return;
                      }
                      const WorkspaceList::Workspace* workspace =
                          WorkspaceList::Get().Find(id);
                      Answer(std::move(request), std::move(reply), profile,
                             workspace ? workspace->name : std::string());
                    },
                    std::move(request), std::move(reply)));
}

}  // namespace uw
