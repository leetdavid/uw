// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_MCP_CLIENT_H_
#define UW_BROWSER_CONNECTORS_MCP_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "uw/components/connectors/mcp.h"

namespace uw {

// How one request to a connector ended: its answer, or why there is none.
struct McpReply {
  McpReply();
  McpReply(McpReply&&);
  McpReply& operator=(McpReply&&);
  ~McpReply();

  static McpReply Problem(std::string problem);

  std::optional<mcp::Message> message;
  // What went wrong when there is no answer, for the user.
  std::string problem;
  bool timed_out = false;
  // A web connector's HTTP status, when it sent no answer.
  int http_status = 0;
  // A web connector asked for a sign-in, and where it says to find out how.
  bool needs_sign_in = false;
  std::string resource_metadata;
};

// The wire to one connector: a program's input and output, or a web address.
class McpTransport {
 public:
  using Done = base::OnceCallback<void(McpReply)>;

  virtual ~McpTransport() = default;
  // Sends a request. `version` is the MCP version it speaks, empty for the
  // legacy handshake itself. `name` is its tool's or prompt's name, if
  // any. A web connector sends both as headers too.
  virtual void Send(int id,
                    std::string_view version,
                    mcp::Era era,
                    std::string_view method,
                    std::string_view name,
                    std::string body,
                    Done done) = 0;
  virtual void Notify(std::string body) = 0;
  // Drops the request `id`; its answer, if one comes, is ignored.
  virtual void Forget(int id) = 0;
};

// Speaks MCP to one connector over `transport`: works out which generation
// of MCP it speaks, lists what it offers, and sends requests.
class McpClient {
 public:
  struct Listing {
    Listing();
    Listing(Listing&&);
    Listing& operator=(Listing&&);
    ~Listing();

    mcp::Era era = mcp::Era::kModern;
    std::string version;
    std::vector<mcp::Tool> tools;
    std::vector<mcp::Prompt> prompts;
  };
  using Connected = base::OnceCallback<void(base::expected<Listing, McpReply>)>;

  explicit McpClient(std::unique_ptr<McpTransport> transport);
  McpClient(const McpClient&) = delete;
  McpClient& operator=(const McpClient&) = delete;
  ~McpClient();

  // Finds out which MCP the connector speaks, trying `era` first when uw
  // already knows it, then lists its tools and prompts.
  void Connect(std::optional<mcp::Era> era, Connected done);
  // Lists the tools and prompts again, after the connector said they
  // changed.
  void List(Connected done);
  // A request once connected. `name` is the tool's or prompt's.
  void Request(std::string_view method,
               base::DictValue params,
               std::string_view name,
               base::TimeDelta timeout,
               McpTransport::Done done);

  McpTransport& transport() { return *transport_; }

 private:
  struct Pending {
    McpTransport::Done done;
    std::unique_ptr<base::OneShotTimer> timer;
  };

  void Send(mcp::Era era,
            std::string_view method,
            base::DictValue params,
            std::string_view name,
            base::TimeDelta timeout,
            McpTransport::Done done);
  void OnReply(int id, McpReply reply);
  void OnTimeout(int id);
  void OnDiscover(Connected done, McpReply reply);
  void Initialize(Connected done);
  void OnInitialize(Connected done, McpReply reply);
  void ListTools(Listing listing,
                 std::optional<std::string> cursor,
                 Connected done);
  void OnTools(Listing listing, Connected done, McpReply reply);
  void ListPrompts(Listing listing,
                   std::optional<std::string> cursor,
                   Connected done);
  void OnPrompts(Listing listing, Connected done, McpReply reply);

  const std::unique_ptr<McpTransport> transport_;
  mcp::Era era_ = mcp::Era::kModern;
  std::string version_ = mcp::kModernVersion;
  int next_id_ = 1;
  std::map<int, Pending> pending_;
  base::WeakPtrFactory<McpClient> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_MCP_CLIENT_H_
