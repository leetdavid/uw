// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_CONNECTORS_H_
#define UW_BROWSER_CONNECTORS_CONNECTORS_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "uw/browser/connectors/mcp_client.h"
#include "uw/components/connectors/connector_config.h"
#include "uw/components/connectors/mcp.h"

namespace base {
class SequencedTaskRunner;
}

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

// The connectors every profile shares. Each one that is on starts when uw
// starts and serves every chat. The list lives in the user data folder, and
// its secrets in Keychain. See docs/plans/connectors.md.
class Connectors {
 public:
  enum class State {
    kOff,
    kStarting,
    kReady,
    kNeedsSignIn,
    kFailed,
  };

  // What uw knows of one connector.
  struct Info {
    Info();
    Info(const Info&);
    Info& operator=(const Info&);
    ~Info();

    // The secrets are left out.
    ConnectorConfig config;
    bool on = true;
    State state = State::kOff;
    // Why it failed, or what it last said.
    std::string error;
    // The MCP version it speaks, once it has answered.
    std::string version;
    // Every tool, those the user turned off included. Kept from its last
    // start while it starts again.
    std::vector<mcp::Tool> tools;
    std::vector<mcp::Prompt> prompts;
    std::set<std::string> tools_off;
    std::set<std::string> always_allowed;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnConnectorsChanged() = 0;
  };

  using Done =
      base::OnceCallback<void(base::expected<mcp::Content, std::string>)>;

  static Connectors& Get();

  Connectors(const Connectors&) = delete;
  Connectors& operator=(const Connectors&) = delete;

  // Reads the list and starts every connector that is on, the first time it
  // is called. `loader` serves web connectors.
  void Start(scoped_refptr<network::SharedURLLoaderFactory> loader);
  bool started() const { return started_; }
  // The saved list has been read; until then nothing can be added.
  bool loaded() const { return read_; }

  const std::vector<Info>& list() const { return list_; }
  const Info* Find(std::string_view name) const;

  // Adds what the user pasted, and starts it. Adds none when a name is
  // taken; the error says which.
  base::expected<void, std::string> Add(std::vector<ConnectorConfig> added);
  void Remove(std::string_view name);
  void SetOn(std::string_view name, bool on);
  void SetToolOn(std::string_view name, std::string_view tool, bool on);
  void SetAlwaysAllowed(std::string_view name,
                        std::string_view tool,
                        bool allowed);
  // Starts a failed connector again.
  void Restart(std::string_view name);

  // Runs a tool. A connector that is still starting runs it once it is
  // ready.
  void CallTool(std::string_view name,
                std::string_view tool,
                base::DictValue arguments,
                Done done);
  void GetPrompt(std::string_view name,
                 std::string_view prompt,
                 base::DictValue arguments,
                 Done done);

  // A local connector's command as a shell reads it, and the environment it
  // runs with, secrets included. Empty for a web connector.
  std::string CommandLine(std::string_view name) const;
  std::map<std::string, std::string> Environment(std::string_view name) const;
  base::FilePath LogFile(std::string_view name) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Keeps the list in `folder` and starts programs through `shell` in place
  // of the user's login shell. Forgets every connector.
  static void SetUpForTesting(const base::FilePath& folder,
                              const base::FilePath& shell);

 private:
  friend class base::NoDestructor<Connectors>;
  struct Live;

  Connectors();
  ~Connectors();

  Info* FindInfo(std::string_view name);
  void OnRead(std::optional<base::DictValue> saved,
              std::map<std::string, base::DictValue> caches);
  void Launch(Info& info);
  void Stop(std::string_view name);
  void OnConnected(std::string name,
                   int generation,
                   base::expected<McpClient::Listing, McpReply> listing);
  void OnExit(std::string name, int generation, std::string last_error);
  void OnNotification(std::string name,
                      int generation,
                      const mcp::Message& message);
  void OnListed(std::string name,
                int generation,
                base::expected<McpClient::Listing, McpReply> listing);
  void Fail(Info& info, std::string error);
  void Request(std::string_view name,
               std::string method,
               base::DictValue params,
               std::string item,
               Done done);
  void SendRequest(Live& live,
                   const std::string& method,
                   base::DictValue params,
                   const std::string& item,
                   Done done);
  void SaveList();
  void SaveCache(const Info& info);
  void Changed();

  base::FilePath folder_;
  base::FilePath shell_;
  scoped_refptr<base::SequencedTaskRunner> files_;
  scoped_refptr<network::SharedURLLoaderFactory> loader_;
  bool started_ = false;
  bool read_ = false;
  std::vector<Info> list_;
  std::map<std::string, std::unique_ptr<Live>, std::less<>> live_;
  // Each era a connector was found to speak, by name.
  std::map<std::string, mcp::Era, std::less<>> eras_;
  // How often each connector started again by itself since the user last
  // started it.
  std::map<std::string, int, std::less<>> restarts_;
  int next_generation_ = 0;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<Connectors> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_CONNECTORS_H_
