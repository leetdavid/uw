// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_AGENT_TOOLS_H_
#define UW_BROWSER_AGENT_AGENT_TOOLS_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/actor/core/task_id.h"
#include "components/tabs/public/tab_interface.h"
#include "uw/components/agent/page_text.h"

namespace actor {
class ActorKeyedService;
class ToolRequest;
}  // namespace actor

namespace uw {

// The tools a chat's agent uses, over Chromium's agent engine. The agent's
// tabs open under the chat's own tab, in the background. Each call takes the
// arguments of a model's tool call and answers with a dictionary, which holds
// "error" when the tool could not do its work.
//
// Tools:
//   open {url} -> {tab}
//   navigate {tab, url} -> {}
//   read {tab} -> {title, url, text}, with each link, field, and button
//       numbered
//   click {tab, element} -> {}
//   type {tab, element, text, enter} -> {}
//
// An element number comes from the latest read of that tab.
class AgentTools {
 public:
  using Done = base::OnceCallback<void(base::DictValue)>;

  explicit AgentTools(tabs::TabInterface& chat);
  AgentTools(const AgentTools&) = delete;
  AgentTools& operator=(const AgentTools&) = delete;
  ~AgentTools();

  void Call(std::string_view tool, const base::DictValue& args, Done done);

  // The agent's open tabs by their numbers.
  std::map<int, tabs::TabInterface*> OpenTabs() const;
  // Gives the agent `tab` back as tab `number`, as it was before uw quit.
  void Adopt(int number, tabs::TabInterface& tab);

 private:
  struct Tab {
    tabs::TabHandle handle;
    PageText page;
  };

  void Open(const base::DictValue& args, Done done);
  void Navigate(const base::DictValue& args, Done done);
  void Read(const base::DictValue& args, Done done);
  void Click(const base::DictValue& args, Done done);
  void Type(const base::DictValue& args, Done done);

  // The agent's tab that `args` names, or null with `error` set.
  Tab* TabFrom(const base::DictValue& args, std::string& error);
  // The element of `tab` that `args` names, or null with `error` set.
  const PageElement* ElementFrom(const Tab& tab,
                                 const base::DictValue& args,
                                 std::string& error);
  void Act(std::unique_ptr<actor::ToolRequest> action,
           base::DictValue result,
           Done done);

  const tabs::TabHandle chat_;
  base::WeakPtr<actor::ActorKeyedService> engine_;
  actor::TaskId task_;
  std::map<int, Tab> tabs_;
  int next_tab_ = 1;
  base::WeakPtrFactory<AgentTools> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_AGENT_TOOLS_H_
