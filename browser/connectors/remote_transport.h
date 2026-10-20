// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_REMOTE_TRANSPORT_H_
#define UW_BROWSER_CONNECTORS_REMOTE_TRANSPORT_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"
#include "uw/browser/connectors/mcp_client.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// A connector at a web address, over MCP's Streamable HTTP: each message is
// a POST, answered with JSON or with an event stream that ends in the
// answer. A legacy server's session id goes with every request after the
// handshake. Redirects are refused, so a key never goes elsewhere.
class RemoteTransport : public McpTransport {
 public:
  // `token` gives the sign-in's access token, if any, for each request.
  using Token = base::RepeatingCallback<std::string()>;

  RemoteTransport(GURL url,
                  std::map<std::string, std::string> headers,
                  scoped_refptr<network::SharedURLLoaderFactory> loader,
                  Token token);
  RemoteTransport(const RemoteTransport&) = delete;
  RemoteTransport& operator=(const RemoteTransport&) = delete;
  ~RemoteTransport() override;

  // McpTransport:
  void Send(int id,
            std::string_view version,
            mcp::Era era,
            std::string_view method,
            std::string_view name,
            std::string body,
            Done done) override;
  void Notify(std::string body) override;
  void Forget(int id) override;

 private:
  void Post(int id,
            std::string_view version,
            mcp::Era era,
            std::string_view method,
            std::string_view name,
            std::string body,
            Done done);
  void OnComplete(int id, std::optional<std::string> body);
  void OnRedirect(int id);
  void Finish(int id, McpReply reply);

  const GURL url_;
  const std::map<std::string, std::string> headers_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  const Token token_;
  // A legacy server's session, from its answer to initialize.
  std::string session_;
  std::string legacy_version_;
  struct Request;
  std::map<int, std::unique_ptr<Request>> requests_;
  // Notifications get ids of their own below zero, so they never clash.
  int next_notification_ = -1;
  base::WeakPtrFactory<RemoteTransport> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_REMOTE_TRANSPORT_H_
