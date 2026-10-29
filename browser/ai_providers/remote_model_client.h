// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_
#define UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "url/gurl.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
struct AiConnectionConfig;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// One-shot HTTPS JSON client for the model providers. POST bodies go
// through Send(); SendGet() covers model listing. Responses are bounded
// (64 KiB, or 5 MiB for a model list) and timed out; a redirect fails the
// request and credentials are never sent. No retries: a failed request
// leaves the branch where it is. Destroying the client cancels the
// in-flight request.
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

  // How the key travels. Anthropic takes it in its own header; a proxy that
  // speaks Anthropic Messages gets it both ways, as proxies read either.
  enum class KeyHeader { kBearer, kAnthropic, kBoth };
  static KeyHeader KeyHeaderFor(ProviderPreset preset, ProviderFormat format);

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

  // A request to the language model in `model`, in its provider's format and
  // at its effort. `chat_body` is in chat completions' shape, and so is the
  // body of a successful outcome. See uw/components/ai_providers/formats.h.
  static std::unique_ptr<RemoteModelClient> ForLanguageModel(
      scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
      const AiConnectionConfig& model,
      std::string api_key,
      const std::string& chat_body,
      base::TimeDelta timeout);

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
  // The key's hash, which outlives it, for RejectedKeys.
  std::string key_hash_;
  std::string request_body_;
  base::TimeDelta timeout_;
  KeyHeader key_header_;
  // Set for a language model that does not speak chat completions: its
  // answers are read back into that shape.
  std::optional<ProviderFormat> answer_format_;
  // The base address and the model the answer came from, which mark what it
  // kept.
  std::string address_;
  std::string model_;
  ResultCallback callback_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::WeakPtrFactory<RemoteModelClient> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_REMOTE_MODEL_CLIENT_H_
