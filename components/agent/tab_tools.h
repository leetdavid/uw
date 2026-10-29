// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_TAB_TOOLS_H_
#define UW_COMPONENTS_AGENT_TAB_TOOLS_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace uw {

// The tools that list, show, move, and close the user's tabs and rename their
// tasks, without reading the pages. See docs/plans/tab-tools.md.
inline constexpr char kTabsTool[] = "tabs";
inline constexpr char kSelectTool[] = "select";
inline constexpr char kMoveTool[] = "move";
inline constexpr char kRenameTaskTool[] = "rename_task";
inline constexpr char kCloseTool[] = "close";
inline constexpr char kCloseTaskTool[] = "close_task";

bool IsTabTool(std::string_view tool);

// The tab tools as a model is told about them. A run, which nobody watches,
// has no select.
base::ListValue TabToolSchemas(bool select);

// One of the user's tabs, as the list shows it.
struct ListedTab {
  // The page's id in the tab tree, or a pin's id.
  std::string id;
  std::string title;
  std::string url;
  // The workspace's name, and which of its tab trees holds the tab: 1, or
  // more for another window that keeps its own tabs. Empty for a pin.
  std::string workspace;
  int tree = 1;
  // The task's id and name. Empty in the Scratchpad and for a pin.
  std::string task_id;
  std::string task;
  // Levels under parent tabs.
  int depth = 0;
  bool pinned = false;
  bool selected = false;
  bool kept = false;
  // A terminal running a command.
  bool busy = false;
  // The agent's own tab by its number, or 0.
  int agent_tab = 0;
  // The chat's own tab.
  bool chat = false;
  // When the user last looked at it.
  base::Time seen;
};

// Ids print as their first characters, and any unique start of one names it.
inline constexpr size_t kShortIdLength = 8;
std::string ShortId(std::string_view id);

// Where a tab sits, as a heading of the list: "Pinned", or the workspace,
// with its window when it has several trees, then the task or the Scratchpad.
std::string PlaceOf(const ListedTab& tab);

// What a tab's address says of it: its site, "terminal", or a uw page's
// address.
std::string SiteOf(std::string_view url);

// How long ago `seen` was, such as "3 days ago".
std::string SeenAgo(base::Time seen, base::Time now);

// What the tabs tool keeps: tabs whose title or address holds every word of
// `query`, in the workspace and the task named, by name or by id.
struct TabFilter {
  std::string query;
  std::string workspace;
  std::string task;
};
TabFilter TabFilterFrom(const base::DictValue& args);
bool Matches(const ListedTab& tab, const TabFilter& filter);

// The tabs tool's answer: how many tabs, in how many workspaces, and the
// list, pins first, then each workspace's tasks and Scratchpad, one line per
// tab, at most kMaxListedTabs.
inline constexpr size_t kMaxListedTabs = 300;
base::DictValue TabListResult(const std::vector<ListedTab>& tabs,
                              base::Time now);

// A tab as a step's answer names it.
base::DictValue TabValue(const ListedTab& tab);

// Why a tab tool did not act on a tab, as the chat shows it after the tab:
// "Didn't close ~/dev/shop-web: a command runs there."
struct TabRefusal {
  std::string reason;
  // The tab it is about, or the id it could not find.
  std::string tab;
};
base::DictValue RefusalResult(std::string_view tool, const TabRefusal& refusal);

// The tabs that `args` names in "tabs", each by any unique start of its id,
// or by the agent's number for one of its own tabs, as indexes into `tabs`.
base::expected<std::vector<size_t>, TabRefusal> FindTabs(
    const std::vector<ListedTab>& tabs,
    const base::DictValue& args);

// The tab that `args` names in "tab".
base::expected<size_t, TabRefusal> FindTab(const std::vector<ListedTab>& tabs,
                                           const base::DictValue& args);

// A tab whose task `args` names in `key`, by any unique start of the task's
// id or by its whole name.
base::expected<size_t, TabRefusal> FindTask(const std::vector<ListedTab>& tabs,
                                            const base::DictValue& args,
                                            std::string_view key);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_TAB_TOOLS_H_
