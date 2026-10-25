// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/components/connectors/connector_config.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

using State = Connectors::State;

// How long `add` waits for new connectors to start, and `sign-in` for the
// user to finish signing in.
constexpr base::TimeDelta kStartWait = base::Seconds(60);
constexpr base::TimeDelta kSignInWait = base::Minutes(10);

// Waits until `ready` holds as the connectors change, at the latest until
// `timeout`, then tells `done` whether it held.
class ConnectorsWait : public Connectors::Observer {
 public:
  static void Start(base::RepeatingCallback<bool()> ready,
                    base::TimeDelta timeout,
                    base::OnceCallback<void(bool)> done) {
    if (ready.Run()) {
      std::move(done).Run(true);
      return;
    }
    new ConnectorsWait(std::move(ready), timeout, std::move(done));
  }

  void OnConnectorsChanged() override {
    if (ready_.Run()) {
      Finish(true);
    }
  }

 private:
  ConnectorsWait(base::RepeatingCallback<bool()> ready,
                 base::TimeDelta timeout,
                 base::OnceCallback<void(bool)> done)
      : ready_(std::move(ready)), done_(std::move(done)) {
    observation_.Observe(&Connectors::Get());
    timer_.Start(FROM_HERE, timeout,
                 base::BindOnce(&ConnectorsWait::Finish,
                                base::Unretained(this), false));
  }

  void Finish(bool met) {
    if (!done_) {
      return;
    }
    observation_.Reset();
    timer_.Stop();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done_), met));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               this);
  }

  base::RepeatingCallback<bool()> ready_;
  base::OnceCallback<void(bool)> done_;
  base::OneShotTimer timer_;
  base::ScopedObservation<Connectors, Connectors::Observer> observation_{this};
};

// Runs `then` once uw has read the connectors, starting them first when no
// window has yet, as when the command started uw.
void WhenLoaded(base::OnceClosure then) {
  Connectors::Get().Start();
  ConnectorsWait::Start(
      base::BindRepeating([] { return Connectors::Get().loaded(); }),
      kStartWait,
      base::BindOnce([](base::OnceClosure then, bool) { std::move(then).Run(); },
                     std::move(then)));
}

std::string_view StateName(State state) {
  switch (state) {
    case State::kOff:
      return "off";
    case State::kStarting:
      return "starting";
    case State::kReady:
      return "ready";
    case State::kNeedsSignIn:
      return "needs-sign-in";
    case State::kSigningIn:
      return "signing-in";
    case State::kCantSignIn:
      return "cant-sign-in";
    case State::kFailed:
      return "failed";
  }
}

base::Value Describe(const Connectors::Info& info) {
  const ConnectorConfig& config = info.config;
  base::ListValue secrets;
  for (const auto& [key, value] : config.env) {
    secrets.Append(key);
  }
  for (const auto& [key, value] : config.headers) {
    secrets.Append(key);
  }
  int tools = 0;
  for (const mcp::Tool& tool : info.tools) {
    tools += info.tools_off.contains(tool.name) ? 0 : 1;
  }
  base::DictValue described;
  described.Set("name", config.name);
  described.Set("kind", config.remote() ? "web" : "local");
  described.Set("address", config.remote()
                               ? config.url
                               : Connectors::Get().CommandLine(config.name));
  described.Set("state", StateName(info.state));
  described.Set("error", info.error);
  described.Set("tools", tools);
  described.Set("secrets", std::move(secrets));
  return base::Value(std::move(described));
}

base::Value DescribeAll(const std::vector<std::string>& names) {
  base::ListValue described;
  for (const std::string& name : names) {
    if (const Connectors::Info* info = Connectors::Get().Find(name)) {
      described.Append(Describe(*info));
    }
  }
  base::DictValue result;
  result.Set("connectors", std::move(described));
  return base::Value(std::move(result));
}

bool Settled(const std::vector<std::string>& names) {
  for (const std::string& name : names) {
    const Connectors::Info* info = Connectors::Get().Find(name);
    if (info &&
        (info->state == State::kStarting || info->state == State::kSigningIn)) {
      return false;
    }
  }
  return true;
}

void List(CommandRequest request, CommandReply reply) {
  std::vector<std::string> names;
  for (const Connectors::Info& info : Connectors::Get().list()) {
    names.push_back(info.config.name);
  }
  std::move(reply).Run(CommandResult(request.id, DescribeAll(names)));
}

void Add(CommandRequest request, CommandReply reply) {
  const std::string* json = request.params.FindString("json");
  base::expected<std::vector<ConnectorConfig>, std::string> read =
      json ? ParseConnectors(*json)
           : base::unexpected(std::string("uw mcp add needs a connector."));
  if (!read.has_value()) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, read.error()));
    return;
  }
  std::vector<std::string> names;
  for (const ConnectorConfig& config : *read) {
    names.push_back(config.name);
  }
  Connectors::Get().AddPasted(
      std::move(read).value(),
      base::BindOnce(
          [](base::Value id, std::vector<std::string> names, CommandReply reply,
             const std::string& error) {
            if (!error.empty()) {
              std::move(reply).Run(CommandError(id, kCommandUsage, error));
              return;
            }
            ConnectorsWait::Start(
                base::BindRepeating(&Settled, names), kStartWait,
                base::BindOnce(
                    [](base::Value id, std::vector<std::string> names,
                       CommandReply reply, bool) {
                      std::move(reply).Run(
                          CommandResult(id, DescribeAll(names)));
                    },
                    std::move(id), names, std::move(reply)));
          },
          request.id.Clone(), std::move(names), std::move(reply)));
}

// The connector `request` names, or the reply that says none has that name.
const Connectors::Info* Named(const CommandRequest& request,
                              CommandReply& reply) {
  const std::string* name = request.params.FindString("name");
  const Connectors::Info* info = name ? Connectors::Get().Find(*name) : nullptr;
  if (!info) {
    std::move(reply).Run(CommandError(
        request.id, kCommandUsage,
        base::StrCat({"No connector is called ", name ? *name : "",
                      ".\n  uw mcp list"})));
  }
  return info;
}

void Remove(CommandRequest request, CommandReply reply) {
  const Connectors::Info* info = Named(request, reply);
  if (!info) {
    return;
  }
  const std::string name = info->config.name;
  Connectors::Get().Remove(name);
  base::DictValue removed;
  removed.Set("name", name);
  std::move(reply).Run(
      CommandResult(request.id, base::Value(std::move(removed))));
}

// Opens the sign-in page in the front window of the workspace the command
// names, brought forward, as the sign-in on uw://ai/connectors does in its
// own window.
content::WebContents* OpenSignInTab(Profile* profile, const GURL& url) {
  BrowserWindowInterface* window = profile ? ShowFrontWindow(profile) : nullptr;
  if (!window) {
    return nullptr;
  }
  NavigateParams params(window, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  return params.navigated_or_inserted_contents.get();
}

void SignIn(CommandRequest request, CommandReply reply) {
  const Connectors::Info* info = Named(request, reply);
  if (!info) {
    return;
  }
  const std::string name = info->config.name;
  if (!info->config.remote()) {
    std::move(reply).Run(CommandError(
        request.id, kCommandUsage,
        base::StrCat({name, " runs on this Mac and needs no sign-in."})));
    return;
  }
  if (info->state == State::kReady) {
    std::move(reply).Run(CommandResult(request.id, DescribeAll({name})));
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
          [](base::Value id, std::string name, CommandReply reply,
             Profile* profile, const std::string&) {
            Connectors::Get().SignIn(name,
                                     base::BindOnce(&OpenSignInTab,
                                                    base::Unretained(profile)));
            ConnectorsWait::Start(
                base::BindRepeating(&Settled, std::vector<std::string>{name}),
                kSignInWait,
                base::BindOnce(
                    [](base::Value id, std::string name, CommandReply reply,
                       bool) {
                      const Connectors::Info* info =
                          Connectors::Get().Find(name);
                      if (info && info->state == State::kReady) {
                        std::move(reply).Run(
                            CommandResult(id, DescribeAll({name})));
                        return;
                      }
                      std::move(reply).Run(CommandError(
                          id, kCommandFailed,
                          base::StrCat(
                              {"The sign-in to ", name, " didn't finish",
                               info && !info->error.empty()
                                   ? base::StrCat({": ", info->error})
                                   : std::string(),
                               ". Try again: uw mcp sign-in ", name})));
                    },
                    std::move(id), std::move(name), std::move(reply)));
          },
          request.id.Clone(), name, std::move(reply)));
}

}  // namespace

void RunConnectorsCommand(CommandRequest request, CommandReply reply) {
  WhenLoaded(base::BindOnce(
      [](CommandRequest request, CommandReply reply) {
        if (request.method == "connectors.list") {
          List(std::move(request), std::move(reply));
        } else if (request.method == "connectors.add") {
          Add(std::move(request), std::move(reply));
        } else if (request.method == "connectors.remove") {
          Remove(std::move(request), std::move(reply));
        } else if (request.method == "connectors.signIn") {
          SignIn(std::move(request), std::move(reply));
        } else {
          std::move(reply).Run(CommandError(
              request.id, kMethodNotFound,
              base::StrCat({"This uw can't answer ", request.method,
                            ". Update uw, then try again."})));
        }
      },
      std::move(request), std::move(reply)));
}

}  // namespace uw
