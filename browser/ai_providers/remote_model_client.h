// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_
#define UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// One-shot HTTPS JSON client for the classification provider. POST bodies go
// through Send(); SendGet() covers model listing. Responses are bounded
// (64 KiB) and timed out; a redirect fails the request and credentials are
// never sent. No retries: a failed request leaves the branch where it is.
// Destroying the client cancels the in-flight request.
class RemoteModelClient {
 public:
  struct Outcome {
    // True when transport succeeded with a 2xx status and a complete body.
    bool ok = false;
    // A net::Error. It can be net::OK while `ok` is false: the provider
    // answered with an error status.
    int net_error = 0;
    // 0 when the provider sent no status.
    int http_status = 0;
    // Whatever the provider sent, also with an error status.
    std::string body;
  };
  using ResultCallback = base::OnceCallback<void(Outcome)>;

  // How the key travels. Anthropic takes it in its own header.
  enum class KeyHeader { kBearer, kAnthropic };

  RemoteModelClient(
      scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
      GURL endpoint,
      std::string api_key,
      std::string request_body,
      base::TimeDelta timeout,
      KeyHeader key_header = KeyHeader::kBearer);
  ~RemoteModelClient();
  RemoteModelClient(const RemoteModelClient&) = delete;
  RemoteModelClient& operator=(const RemoteModelClient&) = delete;

  void Send(ResultCallback callback);
  void SendGet(ResultCallback callback);

 private:
  void StartRequest(bool post, ResultCallback callback);
  void OnRedirect();
  void OnComplete(std::optional<std::string> body);
  void Finish(Outcome outcome);

  scoped_refptr<network::SharedURLLoaderFactory> loader_factory_;
  GURL endpoint_;
  // Never logged.
  std::string api_key_;
  std::string request_body_;
  base::TimeDelta timeout_;
  KeyHeader key_header_;
  ResultCallback callback_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::WeakPtrFactory<RemoteModelClient> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_
