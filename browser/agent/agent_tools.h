// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_AGENT_TOOLS_H_
#define UW_BROWSER_AGENT_AGENT_TOOLS_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/actor/core/task_id.h"
#include "components/tabs/public/tab_interface.h"
#include "uw/components/agent/page_text.h"
#include "uw/components/terminal/shell_host.h"

namespace actor {
class ActorKeyedService;
class ToolRequest;
}  // namespace actor

namespace url {
class Origin;
}  // namespace url

namespace uw {

class TerminalShell;

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
//   run {command, tab, new_tab, wait} -> {tab, folder, output, running}
//   screen {tab} -> {tab, title, folder, text, program, password}
//   keys {tab, text, keys} -> {tab, text}
//
// An element number comes from the latest read of that tab. The terminal
// tools work only in terminal tabs the agent opened, which run the user's
// login shell; see docs/plans/agent-chats.md.
class AgentTools {
 public:
  using Done = base::OnceCallback<void(base::DictValue)>;

  explicit AgentTools(tabs::TabInterface& chat);
  AgentTools(const AgentTools&) = delete;
  AgentTools& operator=(const AgentTools&) = delete;
  ~AgentTools();

  void Call(std::string_view tool, const base::DictValue& args, Done done);

  // A step the user must approve first: its tab and site, what it does, and
  // why it needs approval. A terminal step has its folder in place of a site,
  // and its command, or the text and keys it sends with what the program
  // last showed.
  struct Approval {
    int tab = 0;
    std::string site;
    std::string action;
    std::string reason;
    std::string folder;
    std::string command;
    std::string text;
    std::vector<std::string> keys;
    std::string screen;
    // A connector's tool: the connector, the tool, and the arguments the
    // model sent, as JSON.
    std::string connector;
    std::string tool;
    std::string arguments;
    // A terminal step that may read a sensitive file, and the file, when the
    // command check named it.
    bool sensitive = false;
    std::string file;
  };
  // Whether `tool` works in a terminal tab. Outside Ask first, the command
  // check decides whether a terminal step asks.
  static bool IsTerminalTool(std::string_view tool);
  // What a call to `tool` with `args` needs approved: clicking a button
  // that submits a form, pressing Enter in a form's field, typing a password
  // or payment details, and any click or typing the model marks with
  // "changes". With `every_step`, any other click or typing as well. Every
  // command and every set of keys. Nothing for any other step.
  std::optional<Approval> ApprovalFor(std::string_view tool,
                                      const base::DictValue& args,
                                      bool every_step = false);
  // Asks the user whether the agent may act on a site that Chromium marks as
  // sensitive, in the middle of a step, and answers true if they approve.
  // Without one, the engine refuses such a site.
  using ConfirmSite =
      base::RepeatingCallback<void(Approval, base::OnceCallback<void(bool)>)>;
  void SetConfirmSite(ConfirmSite confirm_site) {
    confirm_site_ = std::move(confirm_site);
  }

  // Tells the chat that a program in tab `number` waits for a password,
  // which only the user types, or with 0, that nothing waits any more.
  using OnPassword =
      base::RepeatingCallback<void(int number, const std::string& program)>;
  void SetOnPassword(OnPassword on_password) {
    on_password_ = std::move(on_password);
  }
  // Ends a wait for a command or keys at once. The command keeps running in
  // its tab, and the step answers with what the tab shows so far.
  void EndWait();

  // The agent's open tabs by their numbers.
  std::map<int, tabs::TabInterface*> OpenTabs() const;
  // Whether the agent's tab `number` is one of its terminal tabs.
  bool IsTerminalTab(int number) const;
  // Gives the agent `tab` back as tab `number`, as it was before uw quit. A
  // terminal tab is not given back: its shell started again, empty.
  void Adopt(int number, tabs::TabInterface& tab);

 private:
  struct Tab {
    tabs::TabHandle handle;
    PageText page;
    bool terminal = false;
    // What the terminal tab showed after the agent's last step there.
    std::string screen;
  };
  class EngineDelegate;
  // A command or keys under way in a terminal tab, or a new terminal tab
  // starting its shell.
  struct TerminalWait {
    int tab = 0;
    // Keys for a program, not a command for the shell.
    bool keys = false;
    // The run opened the tab, so its shell may still be starting.
    bool opened = false;
    // Nothing is sent yet.
    bool starting = true;
    std::string command;
    // What goes to the shell once it can take it.
    std::string bytes;
    base::TimeDelta limit;
    // When the wait began, and from the send on, when the bytes went.
    base::TimeTicks since;
    // How long the user took over a password, which the limit leaves out.
    base::TimeDelta password_time;
    base::TimeTicks password_since;
    // The command has been seen running, away from the prompt.
    bool left_prompt = false;
    bool end_now = false;
    // The line the command was typed on.
    int from_line = 0;
    Done done;
  };

  // The engine asks about `site` in the step under way.
  void AskAboutSite(const url::Origin& site,
                    bool sensitive,
                    base::OnceCallback<void(bool)> done);

  void Open(const base::DictValue& args, Done done);
  void Navigate(const base::DictValue& args, Done done);
  void Read(const base::DictValue& args, Done done);
  void Click(const base::DictValue& args, Done done);
  void Type(const base::DictValue& args, Done done);
  void Run(const base::DictValue& args, Done done);
  void Screen(const base::DictValue& args, Done done);
  void Keys(const base::DictValue& args, Done done);
  std::optional<Approval> TerminalApprovalFor(std::string_view tool,
                                              const base::DictValue& args,
                                              bool every_step);

  // The agent's tab that `args` names, or null with `error` set.
  Tab* TabFrom(const base::DictValue& args, std::string& error);
  // The same, for a tool that works only in a web tab, or only in a terminal
  // tab the agent opened.
  Tab* WebTabFrom(const base::DictValue& args, std::string& error);
  Tab* TerminalTabFrom(const base::DictValue& args, std::string& error);
  // The terminal tab a run uses: the one `args` names, else the agent's
  // first one. 0 when the run opens a new one, and -1 with `error` set.
  int TerminalFor(const base::DictValue& args, std::string& error);
  // Opens a terminal tab under the chat and answers with its number, or 0.
  int OpenTerminal();
  // The folder of terminal tab `number` as the user reads it, such as
  // ~/Downloads.
  std::string FolderOf(int number) const;

  // The shell of the agent's tab `number`, or null.
  TerminalShell* ShellOf(int number) const;

  // A terminal step's wait checks on its tab's shell every so often.
  void CheckShell();
  void OnActivity(ShellHost::Activity activity);
  void Send();
  void Collect(bool ended);
  void OnCollected(bool ended, const std::string& text, int cursor_line);
  void FinishWait(base::DictValue result);
  // The element of `tab` that `args` names, or null with `error` set.
  const PageElement* ElementFrom(const Tab& tab,
                                 const base::DictValue& args,
                                 std::string& error);
  void Act(std::unique_ptr<actor::ToolRequest> action,
           base::DictValue result,
           Done done);

  const tabs::TabHandle chat_;
  const std::unique_ptr<EngineDelegate> delegate_;
  base::WeakPtr<actor::ActorKeyedService> engine_;
  actor::TaskId task_;
  std::map<int, Tab> tabs_;
  int next_tab_ = 1;
  // The tab the step under way acts in.
  int acting_tab_ = 0;
  ConfirmSite confirm_site_;
  OnPassword on_password_;
  std::unique_ptr<TerminalWait> wait_;
  base::OneShotTimer check_;
  base::WeakPtrFactory<AgentTools> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_AGENT_TOOLS_H_
