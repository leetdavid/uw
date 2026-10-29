// Copyright 2026 The uw Authors. All rights reserved.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "uw/browser/agent/user_tabs.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/components/agent/tab_tools.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

// Each command's tab tool, and its verb in a refusal.
struct Tool {
  std::string_view method;
  std::string_view tool;
  std::string_view verb;
};
constexpr auto kTools = std::to_array<Tool>({
    {"tabs.select", kSelectTool, "show"},
    {"tabs.move", kMoveTool, "move"},
    {"tabs.rename-task", kRenameTaskTool, "rename"},
    {"tabs.close", kCloseTool, "close"},
    {"tabs.close-task", kCloseTaskTool, "close"},
});

// A tab as uw tabs list --json prints it.
base::DictValue Listed(const ListedTab& tab) {
  base::DictValue value;
  value.Set("id", ShortId(tab.id));
  value.Set("title", tab.title);
  value.Set("url", tab.url);
  value.Set("place", PlaceOf(tab));
  value.Set("workspace", tab.pinned ? std::string() : tab.workspace);
  if (tab.task_id.empty()) {
    value.Set("task", base::Value());
  } else {
    value.Set("task", base::DictValue()
                          .Set("id", ShortId(tab.task_id))
                          .Set("name", tab.task));
  }
  value.Set("parent", tab.parent.empty() ? base::Value()
                                         : base::Value(ShortId(tab.parent)));
  value.Set("depth", tab.depth);
  value.Set("selected", tab.selected);
  value.Set("kept", tab.kept);
  value.Set("pinned", tab.pinned);
  value.Set("busy", tab.busy);
  value.Set("seen", tab.seen.is_null()
                        ? base::Value()
                        : base::Value(base::TimeFormatAsIso8601(tab.seen)));
  return value;
}

void List(CommandRequest request,
          CommandReply reply,
          std::string workspace,
          std::vector<ListedTab> tabs) {
  TabFilter filter;
  if (const std::string* query = request.params.FindString("query")) {
    filter.query = *query;
  }
  base::ListValue listed;
  std::map<std::string, int> others;
  for (const ListedTab& tab : tabs) {
    if (!tab.pinned && tab.workspace != workspace) {
      ++others[tab.workspace];
      continue;
    }
    if (Matches(tab, filter)) {
      listed.Append(Listed(tab));
    }
  }
  base::ListValue elsewhere;
  for (const auto& [name, count] : others) {
    elsewhere.Append(
        base::DictValue().Set("workspace", name).Set("tabs", count));
  }
  std::move(reply).Run(CommandResult(
      request.id, base::Value(base::DictValue()
                                  .Set("workspace", std::move(workspace))
                                  .Set("tabs", std::move(listed))
                                  .Set("others", std::move(elsewhere)))));
}

// A refusal the person fixes by typing the command again differently.
bool IsUsage(std::string_view reason) {
  return reason.starts_with("pass ") || reason.starts_with("it matches ") ||
         reason == "no tab has that id" || reason == "no task has that id";
}

// What a refusal says in a terminal, such as "didn't close e2b9f604
// (~/dev/shop-web): a command runs there".
std::string Said(const Tool& tool, const base::DictValue& refused) {
  const std::string* reason = refused.FindString("refused");
  const std::string* tab = refused.FindString("tab");
  const std::string* id = refused.FindString("id");
  if (const base::ListValue* matches = refused.FindList("matches")) {
    std::vector<std::string> names;
    for (const base::Value& match : *matches) {
      names.push_back(match.GetString());
    }
    return base::StrCat(
        {*tab, " ", reason->substr(3), ": ", base::JoinString(names, ", ")});
  }
  if (id && tab) {
    return base::StrCat(
        {"didn't ", tool.verb, " ", *id, " (", *tab, "): ", *reason});
  }
  std::string error = *refused.FindString("error");
  error[0] = base::ToLowerASCII(error[0]);
  base::TrimString(error, ".", &error);
  return error;
}

void Run(CommandRequest request,
         CommandReply reply,
         size_t index,
         Profile* profile) {
  const Tool& tool = kTools[index];
  base::DictValue args = request.params.Clone();
  args.Remove("workspace");
  RunTabTool(
      profile, tool.tool, args, TabOwners(),
      base::BindOnce(
          [](CommandRequest request, CommandReply reply, size_t index,
             base::DictValue result) {
            const Tool& tool = kTools[index];
            if (result.FindString("refused")) {
              std::move(reply).Run(CommandError(
                  request.id,
                  IsUsage(*result.FindString("refused")) ? kCommandUsage
                                                         : kCommandFailed,
                  Said(tool, result)));
              return;
            }
            if (const std::string* error = result.FindString("error")) {
              std::move(reply).Run(
                  CommandError(request.id, kCommandFailed, *error));
              return;
            }
            std::move(reply).Run(
                CommandResult(request.id, base::Value(std::move(result))));
          },
          std::move(request), std::move(reply), index),
      /*bring_forward=*/tool.tool == kSelectTool);
}

}  // namespace

void RunTabsCommand(CommandRequest request, CommandReply reply) {
  std::optional<size_t> tool;
  for (size_t i = 0; i < kTools.size(); ++i) {
    if (request.method == kTools[i].method) {
      tool = i;
    }
  }
  if (!tool && request.method != "tabs.list") {
    std::move(reply).Run(
        CommandError(request.id, kMethodNotFound,
                     base::StrCat({"This uw can't answer ", request.method,
                                   ". Update uw, then try again."})));
    return;
  }
  base::expected<CommandWorkspace, std::string> chosen =
      ChooseWorkspace(request.params);
  if (!chosen.has_value()) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, chosen.error()));
    return;
  }
  LoadWorkspace(
      std::move(*chosen),
      base::BindOnce(
          [](CommandRequest request, CommandReply reply,
             std::optional<size_t> tool, Profile* profile,
             const std::string& id) {
            if (!profile) {
              std::move(reply).Run(
                  CommandError(request.id, kCommandFailed,
                               "uw couldn't open that workspace."));
              return;
            }
            if (tool) {
              Run(std::move(request), std::move(reply), *tool, profile);
              return;
            }
            const WorkspaceList::Workspace* workspace =
                WorkspaceList::Get().Find(id);
            ListUserTabs(
                profile,
                base::BindOnce(&List, std::move(request), std::move(reply),
                               workspace ? workspace->name : std::string()));
          },
          std::move(request), std::move(reply), tool));
}

}  // namespace uw
