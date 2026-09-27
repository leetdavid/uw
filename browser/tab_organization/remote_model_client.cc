// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/remote_model_client.h"

#include "base/functional/bind.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace uw {

namespace {

constexpr size_t kMaxResponseBytes = 64 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_tab_organization", R"(
      semantics {
        sender: "uw tab organizer"
        description:
          "Sends tab metadata (titles, URLs, source-tab relationships, task "
          "names) to the user-configured organization provider, or a task- "
          "naming prompt to the user-configured naming model."
        trigger: "A tab opens or changes after the user enables automatic "
          "tab organization and configures both providers."
        data:
          "Tab titles, URLs, source-tab relationships, and task names. "
          "API credentials in the Authorization header."
        destination: OTHER
        destination_other:
          "The user-configured Jev or OpenAI-compatible endpoint."
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
    base::TimeDelta timeout)
    : loader_factory_(std::move(loader_factory)),
      endpoint_(std::move(endpoint)),
      api_key_(std::move(api_key)),
      request_body_(std::move(request_body)),
      timeout_(timeout) {}

RemoteModelClient::~RemoteModelClient() = default;

void RemoteModelClient::Send(ResultCallback callback) {
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = endpoint_;
  resource->method = "POST";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->headers.SetHeader(net::HttpRequestHeaders::kContentType,
                              "application/json");
  resource->headers.SetHeader(net::HttpRequestHeaders::kAuthorization,
                              "Bearer " + api_key_);
  // Drop the secret from memory as soon as the header is set.
  api_key_.assign(api_key_.size(), '\0');
  api_key_.clear();
  loader_ = network::SimpleURLLoader::Create(std::move(resource),
                                             kTrafficAnnotation);
  loader_->SetAllowHttpErrorResults(true);
  loader_->SetTimeoutDuration(timeout_);
  // Reject redirects: following one could send the API key elsewhere.
  loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<RemoteModelClient> self, const GURL&,
         const net::RedirectInfo&,
         const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (self) {
          self->loader_.reset();
        }
      },
      weak_factory_.GetWeakPtr()));
  loader_->AttachStringForUpload(request_body_, "application/json");
  loader_->DownloadToString(
      loader_factory_.get(),
      base::BindOnce(&RemoteModelClient::OnComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback)),
      kMaxResponseBytes);
}

void RemoteModelClient::OnComplete(ResultCallback callback,
                                   std::optional<std::string> body) {
  Outcome outcome;
  const int net_error = loader_->NetError();
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  const int code = head && head->headers ? head->headers->response_code() : -1;
  if (net_error == net::OK && body && code >= 200 && code < 300) {
    outcome.ok = true;
    outcome.body = std::move(*body);
  }
  loader_.reset();
  std::move(callback).Run(std::move(outcome));
}

}  // namespace uw
