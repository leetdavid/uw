// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/remote_model_client.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/components/ai_providers/formats.h"

namespace uw {

namespace {

constexpr size_t kMaxResponseBytes = 64 * 1024;
// Vercel AI Gateway and OpenRouter list their models in over half a megabyte.
constexpr size_t kMaxListBytes =
    network::SimpleURLLoader::kMaxBoundedStringDownloadSize;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_tab_organization", R"(
      semantics {
        sender: "uw tab organizer"
        description:
          "Sends tab metadata (titles, URLs, source-tab relationships, task "
          "names, manual placements) and a short readable-page-text excerpt "
          "to the user-configured OpenAI-compatible or Ollama endpoint for "
          "classification. May also request the endpoint's model list."
        trigger: "A tab opens or changes after the user enables automatic "
          "tab organization and configures one connection."
        data:
          "Tab titles, URLs, source-tab relationships, task names, manual "
          "placements, and a short readable-page-text excerpt. API "
          "credentials in the Authorization header when configured."
        destination: OTHER
        destination_other:
          "The user-configured OpenAI-compatible or Ollama endpoint."
      }
      policy {
        cookies_allowed: NO
        setting: "Automatic organization is off unless the user enables it."
        policy_exception_justification: "Not implemented."
      })");

}  // namespace

RemoteModelClient::RemoteModelClient(
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    GURL endpoint,
    std::string api_key,
    std::string request_body,
    base::TimeDelta timeout,
    KeyHeader key_header)
    : loader_factory_(std::move(loader_factory)),
      endpoint_(std::move(endpoint)),
      api_key_(std::move(api_key)),
      request_body_(std::move(request_body)),
      timeout_(timeout),
      key_header_(key_header) {}

RemoteModelClient::~RemoteModelClient() = default;

// static
RemoteModelClient::KeyHeader RemoteModelClient::KeyHeaderFor(
    ProviderPreset preset,
    ProviderFormat format) {
  if (format != ProviderFormat::kAnthropicMessages) {
    return KeyHeader::kBearer;
  }
  return preset == ProviderPreset::kAnthropic ? KeyHeader::kAnthropic
                                              : KeyHeader::kBoth;
}

// static
std::unique_ptr<RemoteModelClient> RemoteModelClient::ForLanguageModel(
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    const AiConnectionConfig& model,
    std::string api_key,
    const std::string& chat_body,
    base::TimeDelta timeout) {
  const std::string& endpoint = model.endpoint;
  const ProviderFormat format = model.format;
  auto client = std::make_unique<RemoteModelClient>(
      std::move(loader_factory), LanguageModelUrl(endpoint, format),
      std::move(api_key),
      RequestIn(format, WithEffort(chat_body, model.preset, model.effort),
                endpoint)
          .value_or(chat_body),
      timeout, KeyHeaderFor(model.preset, format));
  if (format != ProviderFormat::kChatCompletions) {
    client->answer_format_ = format;
    client->address_ = endpoint;
    client->model_ = model.model;
  }
  return client;
}

void RemoteModelClient::Send(ResultCallback callback) {
  StartRequest(/*post=*/true, std::move(callback));
}

void RemoteModelClient::SendGet(ResultCallback callback) {
  StartRequest(/*post=*/false, std::move(callback));
}

void RemoteModelClient::StartRequest(bool post, ResultCallback callback) {
  callback_ = std::move(callback);
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = endpoint_;
  resource->method = post ? "POST" : "GET";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  if (post) {
    resource->headers.SetHeader(net::HttpRequestHeaders::kContentType,
                                "application/json");
  }
  // Ollama connections carry no key and send no key header.
  if (!api_key_.empty()) {
    if (key_header_ != KeyHeader::kBearer) {
      resource->headers.SetHeader("x-api-key", api_key_);
      resource->headers.SetHeader("anthropic-version", "2023-06-01");
    }
    if (key_header_ != KeyHeader::kAnthropic) {
      resource->headers.SetHeader(net::HttpRequestHeaders::kAuthorization,
                                  "Bearer " + api_key_);
    }
    // Drop the secret from memory as soon as the header is set.
    api_key_.assign(api_key_.size(), '\0');
    api_key_.clear();
  }
  loader_ =
      network::SimpleURLLoader::Create(std::move(resource), kTrafficAnnotation);
  loader_->SetAllowHttpErrorResults(true);
  loader_->SetTimeoutDuration(timeout_);
  // Reject redirects: following one could send the API key elsewhere.
  loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<RemoteModelClient> self, const GURL&,
         const net::RedirectInfo&, const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (self) {
          self->OnRedirect();
        }
      },
      weak_factory_.GetWeakPtr()));
  if (post) {
    loader_->AttachStringForUpload(request_body_, "application/json");
  }
  loader_->DownloadToString(loader_factory_.get(),
                            base::BindOnce(&RemoteModelClient::OnComplete,
                                           weak_factory_.GetWeakPtr()),
                            post ? kMaxResponseBytes : kMaxListBytes);
}

// Runs inside the loader, so the answer goes out from a fresh task.
void RemoteModelClient::OnRedirect() {
  loader_.reset();
  Outcome outcome;
  outcome.net_error = net::ERR_UNSAFE_REDIRECT;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&RemoteModelClient::Finish, weak_factory_.GetWeakPtr(),
                     std::move(outcome)));
}

void RemoteModelClient::OnComplete(std::optional<std::string> body) {
  Outcome outcome;
  outcome.net_error = loader_->NetError();
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  outcome.http_status =
      head && head->headers ? head->headers->response_code() : 0;
  outcome.body = std::move(body).value_or(std::string());
  outcome.ok = outcome.net_error == net::OK && outcome.http_status >= 200 &&
               outcome.http_status < 300;
  if (outcome.ok && answer_format_) {
    if (std::optional<std::string> chat =
            AnswerFrom(*answer_format_, outcome.body, address_, model_)) {
      outcome.body = std::move(*chat);
    }
  }
  loader_.reset();
  Finish(std::move(outcome));
}

void RemoteModelClient::Finish(Outcome outcome) {
  if (callback_) {
    std::move(callback_).Run(std::move(outcome));
  }
}

}  // namespace uw
