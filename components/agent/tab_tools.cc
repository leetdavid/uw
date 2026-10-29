// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/tab_tools.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/types/expected_macros.h"
#include "url/gurl.h"

namespace uw {
namespace {

base::DictValue Tool(std::string_view name,
                     std::string_view description,
                     base::DictValue properties,
                     std::initializer_list<std::string_view> required) {
  base::ListValue names;
  for (std::string_view each : required) {
    names.Append(each);
  }
  return base::DictValue()
      .Set("type", "function")
      .Set("function",
           base::DictValue()
               .Set("name", name)
               .Set("description", description)
               .Set("parameters", base::DictValue()
                                      .Set("type", "object")
                                      .Set("properties", std::move(properties))
                                      .Set("required", std::move(names))));
}

base::DictValue Parameter(std::string_view type, std::string_view description) {
  return base::DictValue().Set("type", type).Set("description", description);
}

base::DictValue Ids(std::string_view description) {
  return base::DictValue()
      .Set("type", "array")
      .Set("items", base::DictValue().Set("type", "string"))
      .Set("description", description);
}

bool IdStartsWith(std::string_view id, std::string_view start) {
  return !start.empty() &&
         base::StartsWith(id, start, base::CompareCase::INSENSITIVE_ASCII);
}

// The line of `tab` in the list, indented by its depth.
std::string LineOf(const ListedTab& tab, base::Time now) {
  std::string line =
      base::StrCat({std::string(2 + 2 * tab.depth, ' '), ShortId(tab.id), "  ",
                    tab.title.empty() ? tab.url : tab.title});
  if (const std::string site = SiteOf(tab.url); !site.empty()) {
    base::StrAppend(&line, {" · ", site});
  }
  if (tab.chat) {
    base::StrAppend(&line, {" · this chat"});
  }
  if (tab.agent_tab) {
    base::StrAppend(&line,
                    {" · your tab ", base::NumberToString(tab.agent_tab)});
  }
  if (tab.busy) {
    base::StrAppend(&line, {" · busy"});
  }
  if (tab.kept) {
    base::StrAppend(&line, {" · kept"});
  }
  if (tab.selected) {
    base::StrAppend(&line, {" · selected"});
  } else if (!tab.seen.is_null()) {
    base::StrAppend(&line, {" · ", SeenAgo(tab.seen, now)});
  }
  return line;
}

std::string Plural(size_t count, std::string_view one, std::string_view many) {
  return base::StrCat(
      {base::NumberToString(count), " ", count == 1 ? one : many});
}

// The ids that `args` holds in "tabs": strings, or numbers for the agent's
// own tabs.
std::vector<base::Value> IdsIn(const base::DictValue& args) {
  std::vector<base::Value> ids;
  if (const base::ListValue* list = args.FindList("tabs")) {
    for (const base::Value& id : *list) {
      ids.push_back(id.Clone());
    }
  }
  return ids;
}

base::expected<size_t, TabRefusal> FindOne(const std::vector<ListedTab>& tabs,
                                           const base::Value& id) {
  if (id.is_int()) {
    for (size_t i = 0; i < tabs.size(); ++i) {
      if (tabs[i].agent_tab == id.GetInt()) {
        return i;
      }
    }
    return base::unexpected(
        TabRefusal{"none of your tabs has that number",
                   base::StrCat({"tab ", base::NumberToString(id.GetInt())})});
  }
  const std::string start = id.is_string()
                                ? std::string(base::TrimWhitespaceASCII(
                                      id.GetString(), base::TRIM_ALL))
                                : std::string();
  std::vector<size_t> found;
  for (size_t i = 0; i < tabs.size(); ++i) {
    if (IdStartsWith(tabs[i].id, start)) {
      found.push_back(i);
    }
  }
  if (found.size() == 1) {
    return found.front();
  }
  if (found.empty()) {
    return base::unexpected(TabRefusal{"no tab has that id", start});
  }
  TabRefusal several{
      base::StrCat(
          {"it matches ", base::NumberToString(found.size()), " tabs"}),
      start};
  for (size_t i : found) {
    several.matches.push_back(base::StrCat(
        {ShortId(tabs[i].id), " (",
         tabs[i].pinned ? std::string("Pinned") : tabs[i].workspace, ")"}));
  }
  return base::unexpected(std::move(several));
}

}  // namespace

bool IsTabTool(std::string_view tool) {
  return tool == kTabsTool || tool == kSelectTool || tool == kMoveTool ||
         tool == kRenameTaskTool || tool == kCloseTool ||
         tool == kCloseTaskTool;
}

base::ListValue TabToolSchemas(bool select) {
  const auto task = [] {
    return Parameter("string", "A task's id from tabs.");
  };
  base::ListValue tools;
  tools.Append(Tool(
      kTabsTool,
      "Lists the user's open tabs in every workspace, by task, each with its "
      "id. You see titles and addresses, never the pages. Every filter is "
      "optional.",
      base::DictValue()
          .Set("query", Parameter("string",
                                  "Words that must all be in a tab's title "
                                  "or address."))
          .Set("workspace", Parameter("string", "A workspace's name."))
          .Set("task", Parameter("string", "A task's id or name.")),
      {}));
  if (select) {
    tools.Append(Tool(kSelectTool,
                      "Shows one of the user's tabs in their window, switching "
                      "to its workspace if needed.",
                      base::DictValue().Set(
                          "tab", Parameter("string", "A tab's id from tabs.")),
                      {"tab"}));
  }
  tools.Append(Tool(
      kMoveTool,
      "Moves the user's tabs, each with the tabs under it, to a task, to a "
      "new task, or to the Scratchpad, where tabs outside every task are. "
      "Tabs move only within their workspace. Pass one of task, new_task, or "
      "scratchpad.",
      base::DictValue()
          .Set("tabs", Ids("Tab ids from tabs."))
          .Set("task", task())
          .Set("new_task", Parameter("string", "The name of a new task."))
          .Set("scratchpad", Parameter("boolean", "True for the Scratchpad.")),
      {"tabs"}));
  tools.Append(Tool(kRenameTaskTool, "Renames one of the user's tasks.",
                    base::DictValue()
                        .Set("task", task())
                        .Set("name", Parameter("string", "The new name.")),
                    {"task", "name"}));
  tools.Append(Tool(kCloseTool,
                    "Closes the user's tabs, each with the tabs under it. A "
                    "terminal running a command is never closed.",
                    base::DictValue().Set("tabs", Ids("Tab ids from tabs.")),
                    {"tabs"}));
  tools.Append(Tool(kCloseTaskTool,
                    "Closes one of the user's tasks and its tabs.",
                    base::DictValue().Set("task", task()), {"task"}));
  return tools;
}

std::string ShortId(std::string_view id) {
  return base::ToLowerASCII(id.substr(0, kShortIdLength));
}

std::string PlaceOf(const ListedTab& tab) {
  if (tab.pinned) {
    return "Pinned";
  }
  std::string place = tab.workspace;
  if (tab.tree > 1) {
    base::StrAppend(&place, {" (window ", base::NumberToString(tab.tree), ")"});
  }
  base::StrAppend(&place,
                  {" › ", tab.task_id.empty() ? "Scratchpad" : tab.task});
  return place;
}

std::string SiteOf(std::string_view url) {
  const GURL gurl(url);
  if (gurl.SchemeIsHTTPOrHTTPS()) {
    const std::string_view host = gurl.host();
    return std::string(base::StartsWith(host, "www.") ? host.substr(4) : host);
  }
  if (gurl.SchemeIs("chrome")) {
    return gurl.host() == "terminal" ? std::string("terminal")
                                     : base::StrCat({"uw://", gurl.host()});
  }
  return std::string();
}

std::string SeenAgo(base::Time seen, base::Time now) {
  const base::TimeDelta ago = now - seen;
  if (ago < base::Minutes(1)) {
    return "now";
  }
  if (ago < base::Hours(1)) {
    return base::StrCat({Plural(ago.InMinutes(), "minute", "minutes"), " ago"});
  }
  if (ago < base::Days(1)) {
    return base::StrCat({Plural(ago.InHours(), "hour", "hours"), " ago"});
  }
  return base::StrCat({Plural(ago.InDays(), "day", "days"), " ago"});
}

TabFilter TabFilterFrom(const base::DictValue& args) {
  TabFilter filter;
  const auto text = [&args](std::string_view key) {
    const std::string* value = args.FindString(key);
    return value
               ? std::string(base::TrimWhitespaceASCII(*value, base::TRIM_ALL))
               : std::string();
  };
  filter.query = text("query");
  filter.workspace = text("workspace");
  filter.task = text("task");
  return filter;
}

bool Matches(const ListedTab& tab, const TabFilter& filter) {
  if (!filter.workspace.empty() &&
      !base::EqualsCaseInsensitiveASCII(tab.workspace, filter.workspace)) {
    return false;
  }
  if (!filter.task.empty() &&
      !base::EqualsCaseInsensitiveASCII(tab.task, filter.task) &&
      !(!tab.task_id.empty() && IdStartsWith(tab.task_id, filter.task))) {
    return false;
  }
  const std::string text =
      base::ToLowerASCII(base::StrCat({tab.title, " ", tab.url}));
  for (std::string_view word :
       base::SplitStringPiece(filter.query, " ", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (text.find(base::ToLowerASCII(word)) == std::string::npos) {
      return false;
    }
  }
  return true;
}

base::DictValue TabListResult(const std::vector<ListedTab>& tabs,
                              base::Time now) {
  std::set<std::string> workspaces;
  std::string list;
  std::string place;
  for (size_t i = 0; i < tabs.size(); ++i) {
    const ListedTab& tab = tabs[i];
    if (!tab.pinned) {
      workspaces.insert(tab.workspace);
    }
    if (i >= kMaxListedTabs) {
      continue;
    }
    if (const std::string here = PlaceOf(tab); here != place) {
      place = here;
      base::StrAppend(&list, {list.empty() ? "" : "\n", place});
      if (!tab.task_id.empty()) {
        base::StrAppend(&list, {" (task ", ShortId(tab.task_id), ")"});
      }
    }
    base::StrAppend(&list, {"\n", LineOf(tab, now)});
  }
  if (tabs.size() > kMaxListedTabs) {
    base::StrAppend(
        &list, {"\n… and ", base::NumberToString(tabs.size() - kMaxListedTabs),
                " more. Narrow the list with query, workspace, or task."});
  }
  if (tabs.empty()) {
    list = "No tabs match.";
  }
  return base::DictValue()
      .Set("tabs", static_cast<int>(tabs.size()))
      .Set("workspaces", static_cast<int>(workspaces.size()))
      .Set("list", std::move(list));
}

base::DictValue TabValue(const ListedTab& tab) {
  return base::DictValue()
      .Set("id", ShortId(tab.id))
      .Set("title", tab.title)
      .Set("url", tab.url);
}

base::DictValue RefusalResult(std::string_view tool,
                              const TabRefusal& refusal) {
  const std::string_view verb = tool == kMoveTool         ? "move"
                                : tool == kSelectTool     ? "show"
                                : tool == kRenameTaskTool ? "rename"
                                                          : "close";
  base::DictValue result =
      base::DictValue()
          .Set("error",
               base::StrCat({"Didn't ", verb, refusal.tab.empty() ? "" : " ",
                             refusal.tab, ": ", refusal.reason, "."}))
          .Set("refused", refusal.reason)
          .Set("tab", refusal.tab);
  if (!refusal.id.empty()) {
    result.Set("id", refusal.id);
  }
  if (!refusal.matches.empty()) {
    base::ListValue matches;
    for (const std::string& match : refusal.matches) {
      matches.Append(match);
    }
    result.Set("matches", std::move(matches));
  }
  return result;
}

base::expected<std::vector<size_t>, TabRefusal> FindTabs(
    const std::vector<ListedTab>& tabs,
    const base::DictValue& args) {
  const std::vector<base::Value> ids = IdsIn(args);
  if (ids.empty()) {
    return base::unexpected(TabRefusal{"pass the tabs' ids from tabs", ""});
  }
  std::vector<size_t> found;
  for (const base::Value& id : ids) {
    ASSIGN_OR_RETURN(const size_t index, FindOne(tabs, id));
    if (!std::ranges::contains(found, index)) {
      found.push_back(index);
    }
  }
  return found;
}

base::expected<size_t, TabRefusal> FindTab(const std::vector<ListedTab>& tabs,
                                           const base::DictValue& args) {
  const base::Value* id = args.Find("tab");
  if (!id) {
    return base::unexpected(TabRefusal{"pass a tab's id from tabs", ""});
  }
  return FindOne(tabs, *id);
}

base::expected<size_t, TabRefusal> FindTask(const std::vector<ListedTab>& tabs,
                                            const base::DictValue& args,
                                            std::string_view key) {
  const std::string* text = args.FindString(key);
  const std::string start =
      text ? std::string(base::TrimWhitespaceASCII(*text, base::TRIM_ALL))
           : std::string();
  if (start.empty()) {
    return base::unexpected(TabRefusal{"pass a task's id from tabs", ""});
  }
  std::map<std::string, size_t> by_id;
  std::map<std::string, size_t> by_name;
  for (size_t i = 0; i < tabs.size(); ++i) {
    if (tabs[i].task_id.empty()) {
      continue;
    }
    if (IdStartsWith(tabs[i].task_id, start)) {
      by_id.try_emplace(tabs[i].task_id, i);
    }
    if (base::EqualsCaseInsensitiveASCII(tabs[i].task, start)) {
      by_name.try_emplace(tabs[i].task_id, i);
    }
  }
  const std::map<std::string, size_t>& found = by_id.empty() ? by_name : by_id;
  if (found.size() == 1) {
    return found.begin()->second;
  }
  if (found.empty()) {
    return base::unexpected(TabRefusal{"no task has that id", start});
  }
  return base::unexpected(
      TabRefusal{base::StrCat({"it matches ",
                               base::NumberToString(found.size()), " tasks"}),
                 start});
}

}  // namespace uw
