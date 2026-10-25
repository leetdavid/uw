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

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/browser/connectors/mcp_client.h"
#include "uw/browser/connectors/sign_in.h"
#include "uw/components/connectors/connector_config.h"
#include "uw/components/connectors/mcp.h"
#include "uw/components/connectors/oauth.h"

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
    kSigningIn,
    // Its sign-in server takes no registration uw can make.
    kCantSignIn,
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
    // The last lines of a failed local connector's log.
    std::vector<std::string> log_lines;
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
  // is called. uw calls it when its first window opens.
  void Start();
  bool started() const { return started_; }
  // The saved list has been read; until then nothing can be added.
  bool loaded() const { return read_; }
  // Why programs on this Mac start without the terminal's environment, when
  // uw could not read it.
  const std::string& environment_problem() const {
    return environment_problem_;
  }
  // The terminal's environment, once uw has read it.
  const std::optional<base::EnvironmentMap>& environment() const {
    return environment_;
  }

  const std::vector<Info>& list() const { return list_; }
  const Info* Find(std::string_view name) const;

  // Adds what the user pasted, and starts it. Adds none when a name is
  // taken; the error says which.
  base::expected<void, std::string> Add(std::vector<ConnectorConfig> added);
  // Adds what the user pasted, as Add does, once each web address of the old
  // HTTP+SSE transport has been tried at /mcp. `done` gets why nothing was
  // added, or an empty sentence.
  using Added = base::OnceCallback<void(const std::string& error)>;
  void AddPasted(std::vector<ConnectorConfig> added, Added done);
  void Remove(std::string_view name);
  void SetOn(std::string_view name, bool on);
  void SetToolOn(std::string_view name, std::string_view tool, bool on);
  void SetAlwaysAllowed(std::string_view name,
                        std::string_view tool,
                        bool allowed);
  // Starts a failed connector again.
  void Restart(std::string_view name);

  // Whether `url` answers as a connector does, or asks for a sign-in as one
  // does.
  void Probe(const GURL& url, base::OnceCallback<void(bool)> done);

  // Opens a web connector's sign-in page with `open`, and starts the
  // connector once the sign-in comes back.
  void SignIn(std::string_view name, ConnectorSignIn::OpenTab open);
  void CancelSignIn(std::string_view name);
  void SignOut(std::string_view name);

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
  void AddAfterProbes(std::vector<ConnectorConfig> connectors,
                      size_t next,
                      Added done);
  void OnProbed(std::vector<ConnectorConfig> connectors,
                size_t index,
                std::string address,
                Added done,
                bool answered);
  void OnEnvironment(std::optional<base::EnvironmentMap> environment);
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
  void OnLogTail(std::string name, std::vector<std::string> lines);
  std::string AccessToken(std::string_view name) const;
  void OnSignedIn(std::string name,
                  base::expected<oauth::Tokens, SignInError> result);
  // Trades the connector's refresh token for a new access token, once
  // until it works again. `done` learns whether it did.
  void Refresh(std::string_view name, base::OnceCallback<void(bool)> done);
  void OnRefreshed(std::string name,
                   base::OnceCallback<void(bool)> done,
                   std::optional<oauth::Tokens> tokens);
  void KeepTokens(std::string_view name, const oauth::Tokens& tokens);
  void ForgetTokens(std::string_view name);
  void Request(std::string_view name,
               std::string method,
               base::DictValue params,
               std::string item,
               Done done);
  void OnAnswer(std::string name,
                std::string method,
                base::DictValue retry,
                std::string item,
                Done done,
                McpReply reply);
  void SaveList();
  void SaveCache(const Info& info);
  void Changed();

  base::FilePath folder_;
  base::FilePath shell_;
  scoped_refptr<base::SequencedTaskRunner> files_;
  scoped_refptr<network::SharedURLLoaderFactory> loader_;
  bool started_ = false;
  bool read_ = false;
  // The terminal's environment, which programs on this Mac start with.
  std::optional<base::EnvironmentMap> environment_;
  std::string environment_problem_;
  std::vector<Info> list_;
  std::map<std::string, std::unique_ptr<Live>, std::less<>> live_;
  // Each era a connector was found to speak, by name.
  std::map<std::string, mcp::Era, std::less<>> eras_;
  // How often each connector started again by itself since the user last
  // started it.
  std::map<std::string, int, std::less<>> restarts_;
  int next_generation_ = 0;
  std::map<std::string, std::unique_ptr<ConnectorSignIn>, std::less<>>
      sign_ins_;
  // Where each web connector's 401 said to read how to sign in.
  std::map<std::string, std::string, std::less<>> sign_in_hints_;
  std::map<std::string, oauth::Tokens, std::less<>> tokens_;
  // Connectors whose token was refreshed since they last worked, which are
  // not refreshed again before they do.
  std::set<std::string, std::less<>> refreshed_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<Connectors> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_CONNECTORS_H_
