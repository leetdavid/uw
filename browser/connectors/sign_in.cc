// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/sign_in.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "uw/browser/ai_providers/api_key_store.h"

namespace uw {
namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_connector_sign_in", R"(
      semantics {
        sender: "uw connectors"
        description:
          "Signs a web connector in with OAuth: reads where its sign-in "
          "server is and how it works, registers uw there, and trades the "
          "code the sign-in page gives for tokens, or refreshes them."
        trigger: "The user clicks Sign in on a connector, or a signed-in "
          "connector's token runs out."
        data: "uw's name and return address, the sign-in code, and tokens."
        destination: OTHER
        destination_other: "The connector's sign-in server."
      }
      policy {
        cookies_allowed: NO
        setting: "No connector signs in until the user clicks Sign in."
        policy_exception_justification: "Not implemented."
      })");

constexpr size_t kMaxAnswer = 1024 * 1024;

std::unique_ptr<network::SimpleURLLoader> Request(const GURL& url,
                                                  std::string body,
                                                  std::string content_type) {
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = url;
  resource->method = body.empty() ? "GET" : "POST";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->headers.SetHeader("Accept", "application/json");
  auto loader =
      network::SimpleURLLoader::Create(std::move(resource), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  if (!body.empty()) {
    loader->AttachStringForUpload(std::move(body), content_type);
  }
  return loader;
}

int Status(const network::SimpleURLLoader& loader) {
  const network::mojom::URLResponseHead* head = loader.ResponseInfo();
  return head && head->headers ? head->headers->response_code() : 0;
}

base::unexpected<SignInError> Error(std::string message,
                                    bool cannot_register = false) {
  return base::unexpected(SignInError{std::move(message), cannot_register});
}

}  // namespace

std::string ClientAccount(std::string_view issuer) {
  return base::StrCat({"connector-client:", issuer});
}

ConnectorSignIn::ConnectorSignIn(
    GURL server,
    GURL resource_metadata,
    scoped_refptr<network::SharedURLLoaderFactory> loader,
    OpenTab open,
    Done done)
    : server_(std::move(server)),
      resource_metadata_(std::move(resource_metadata)),
      loader_(std::move(loader)),
      open_(std::move(open)),
      done_(std::move(done)) {
  std::vector<GURL> urls;
  if (resource_metadata_.is_valid()) {
    urls.push_back(resource_metadata_);
  }
  for (GURL& url : oauth::ResourceMetadataUrls(server_)) {
    urls.push_back(std::move(url));
  }
  TryResourceMetadata(std::move(urls));
}

ConnectorSignIn::~ConnectorSignIn() = default;

void ConnectorSignIn::Fetch(const GURL& url,
                            std::string body,
                            std::string content_type,
                            Fetched done) {
  fetch_ = Request(url, std::move(body), std::move(content_type));
  fetch_->DownloadToString(
      loader_.get(),
      base::BindOnce(&ConnectorSignIn::OnFetched, weak_factory_.GetWeakPtr(),
                     std::move(done)),
      kMaxAnswer);
}

void ConnectorSignIn::OnFetched(Fetched done, std::optional<std::string> body) {
  const int status = Status(*fetch_);
  fetch_.reset();
  std::move(done).Run(status, std::move(body).value_or(std::string()));
}

void ConnectorSignIn::TryResourceMetadata(std::vector<GURL> urls) {
  if (urls.empty()) {
    // Servers from before protected-resource metadata sign in at their own
    // origin.
    std::vector<GURL> issuers =
        oauth::ServerMetadataUrls(server_.GetWithEmptyPath());
    TryServerMetadata(std::move(issuers));
    return;
  }
  const GURL url = urls.front();
  urls.erase(urls.begin());
  Fetch(url, std::string(), std::string(),
        base::BindOnce(&ConnectorSignIn::OnResourceMetadata,
                       weak_factory_.GetWeakPtr(), std::move(urls)));
}

void ConnectorSignIn::OnResourceMetadata(std::vector<GURL> urls,
                                         int status,
                                         std::string body) {
  std::optional<oauth::ResourceMetadata> metadata =
      status == 200 ? oauth::ParseResourceMetadata(body) : std::nullopt;
  if (!metadata) {
    TryResourceMetadata(std::move(urls));
    return;
  }
  scopes_ = metadata->scopes;
  TryServerMetadata(
      oauth::ServerMetadataUrls(metadata->authorization_servers.front()));
}

void ConnectorSignIn::TryServerMetadata(std::vector<GURL> urls) {
  if (urls.empty()) {
    Finish(Error("uw couldn't find this connector's sign-in server."));
    return;
  }
  const GURL url = urls.front();
  urls.erase(urls.begin());
  Fetch(url, std::string(), std::string(),
        base::BindOnce(&ConnectorSignIn::OnServerMetadata,
                       weak_factory_.GetWeakPtr(), std::move(urls)));
}

void ConnectorSignIn::OnServerMetadata(std::vector<GURL> urls,
                                       int status,
                                       std::string body) {
  std::optional<oauth::ServerMetadata> metadata =
      status == 200 ? oauth::ParseServerMetadata(body) : std::nullopt;
  if (!metadata) {
    TryServerMetadata(std::move(urls));
    return;
  }
  if (!metadata->s256) {
    Finish(Error("This connector's sign-in doesn't use PKCE, which uw needs."));
    return;
  }
  metadata_ = std::move(*metadata);
  if (metadata_.issuer.empty()) {
    metadata_.issuer =
        metadata_.authorization_endpoint.GetWithEmptyPath().spec();
  }
  if (std::optional<std::string> saved =
          ApiKeyStore().GetKey(ClientAccount(metadata_.issuer))) {
    if (std::optional<oauth::Client> client = oauth::ParseClient(*saved)) {
      OpenSignInPage(std::move(*client));
      return;
    }
  }
  if (!metadata_.registration_endpoint.is_valid()) {
    Finish(Error(
        "This connector's sign-in needs a registration uw doesn't support yet.",
        /*cannot_register=*/true));
    return;
  }
  Fetch(metadata_.registration_endpoint, oauth::RegistrationBody(),
        "application/json",
        base::BindOnce(&ConnectorSignIn::OnRegistered,
                       weak_factory_.GetWeakPtr()));
}

void ConnectorSignIn::OnRegistered(int status, std::string body) {
  std::optional<oauth::Client> client =
      status / 100 == 2 ? oauth::ParseClient(body) : std::nullopt;
  if (!client) {
    Finish(Error(
        "This connector's sign-in needs a registration uw doesn't support yet.",
        /*cannot_register=*/true));
    return;
  }
  ApiKeyStore().SetKey(ClientAccount(metadata_.issuer), body);
  OpenSignInPage(std::move(*client));
}

void ConnectorSignIn::OpenSignInPage(oauth::Client client) {
  client_ = std::move(client);
  pkce_ = oauth::MakePkce();
  state_ = oauth::MakeState();
  content::WebContents* tab = std::move(open_).Run(oauth::AuthorizationUrl(
      metadata_, client_, pkce_.challenge, state_, server_, scopes_));
  if (!tab) {
    Finish(Error("uw couldn't open the sign-in page."));
    return;
  }
  Observe(tab);
}

void ConnectorSignIn::DidStartNavigation(
    content::NavigationHandle* navigation) {
  Caught(navigation->GetURL());
}

void ConnectorSignIn::DidRedirectNavigation(
    content::NavigationHandle* navigation) {
  Caught(navigation->GetURL());
}

void ConnectorSignIn::WebContentsDestroyed() {
  Observe(nullptr);
  if (done_ && !fetch_) {
    Finish(Error("You closed the sign-in tab."));
  }
}

void ConnectorSignIn::Caught(const GURL& url) {
  std::optional<oauth::Callback> callback = oauth::ParseCallback(url);
  if (!callback || !web_contents()) {
    return;
  }
  content::WebContents* tab = web_contents();
  Observe(nullptr);
  // Closing it inside the navigation's own notification would pull it out
  // from under the navigation.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<content::WebContents> tab) {
                       if (tab) {
                         tab->Close();
                       }
                     },
                     tab->GetWeakPtr()));
  if (!callback->error.empty()) {
    Finish(
        Error(base::StrCat({"The sign-in didn't finish: ", callback->error})));
    return;
  }
  if (callback->state != state_) {
    Finish(Error("The sign-in page sent back the wrong state."));
    return;
  }
  if (!callback->issuer.empty() && callback->issuer != metadata_.issuer) {
    Finish(Error("The sign-in came back from a server uw didn't ask."));
    return;
  }
  Fetch(metadata_.token_endpoint,
        oauth::CodeBody(callback->code, pkce_.verifier, client_, server_),
        "application/x-www-form-urlencoded",
        base::BindOnce(&ConnectorSignIn::OnTokens, weak_factory_.GetWeakPtr()));
}

void ConnectorSignIn::OnTokens(int status, std::string body) {
  oauth::Tokens tokens;
  tokens.token_endpoint = metadata_.token_endpoint;
  tokens.client = client_;
  if (status != 200 || !oauth::ParseTokens(body, base::Time::Now(), tokens)) {
    Finish(Error("The sign-in server refused uw's code."));
    return;
  }
  Finish(std::move(tokens));
}

void ConnectorSignIn::Finish(
    base::expected<oauth::Tokens, SignInError> result) {
  Observe(nullptr);
  if (done_) {
    std::move(done_).Run(std::move(result));
  }
}

// static
void ConnectorSignIn::Refresh(
    oauth::Tokens tokens,
    GURL server,
    scoped_refptr<network::SharedURLLoaderFactory> loader,
    base::OnceCallback<void(std::optional<oauth::Tokens>)> done) {
  if (tokens.refresh.empty() || !tokens.token_endpoint.is_valid()) {
    std::move(done).Run(std::nullopt);
    return;
  }
  std::unique_ptr<network::SimpleURLLoader> fetch =
      Request(tokens.token_endpoint,
              oauth::RefreshBody(tokens.refresh, tokens.client, server),
              "application/x-www-form-urlencoded");
  network::SimpleURLLoader* raw = fetch.get();
  raw->DownloadToString(
      loader.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> fetch,
             oauth::Tokens tokens,
             base::OnceCallback<void(std::optional<oauth::Tokens>)> done,
             std::optional<std::string> body) {
            if (Status(*fetch) != 200 ||
                !oauth::ParseTokens(body.value_or(""), base::Time::Now(),
                                    tokens)) {
              std::move(done).Run(std::nullopt);
              return;
            }
            std::move(done).Run(std::move(tokens));
          },
          std::move(fetch), std::move(tokens), std::move(done)),
      kMaxAnswer);
}

}  // namespace uw
