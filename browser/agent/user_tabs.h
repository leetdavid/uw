// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_USER_TABS_H_
#define UW_BROWSER_AGENT_USER_TABS_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "components/tabs/public/tab_interface.h"
#include "uw/components/agent/tab_tools.h"

class Profile;

namespace uw {

// The user's tabs in one profile's windows, as the tab tools reach them:
// every workspace, hidden windows included, never the run window. A private
// profile reaches only its private windows. See docs/plans/tab-tools.md.

// Tabs a chat knows by more than their id: the agent's own tabs by number,
// and the chat's own tab.
struct TabOwners {
  std::map<int, tabs::TabHandle> agent;
  tabs::TabHandle chat;
};

// A tab tool's call checked against the tabs as they are: the tool's answer
// when it does not act, or else what it changes of the user's own tabs, which
// may need their approval. A call that changes only the agent's own tabs, or
// nothing, changes none of theirs.
struct TabStep {
  std::optional<base::DictValue> refused;
  bool changes_users_tabs = false;
  bool closes = false;
  // What it does, as a question without its question mark, such as "Close 5
  // tabs in Flights", and why it may ask.
  std::string action;
  std::string reason;
  std::string workspace;
  std::string workspace_icon;
  // The tabs it closes or moves.
  std::vector<ListedTab> tabs;
  // A rename's task, as it is named now, and its new name.
  std::string task;
  std::string new_name;
};

void CheckTabStep(Profile* profile,
                  std::string_view tool,
                  const base::DictValue& args,
                  const TabOwners& owners,
                  base::OnceCallback<void(TabStep)> done);

// Shows the tab used last in the task `task` names, as select shows a tab.
void ShowTask(Profile* profile, std::string_view task);

// Every tab of `profile`, as the tabs tool lists them, for the uw command.
void ListUserTabs(Profile* profile,
                  base::OnceCallback<void(std::vector<ListedTab>)> done);

// Runs a tab tool on `profile`'s tabs and answers as the tool does. With
// `bring_forward`, as for the uw command, select brings uw in front too.
void RunTabTool(Profile* profile,
                std::string_view tool,
                const base::DictValue& args,
                const TabOwners& owners,
                base::OnceCallback<void(base::DictValue)> done,
                bool bring_forward = false);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_USER_TABS_H_
