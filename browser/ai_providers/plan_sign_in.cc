// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/plan_sign_in.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/prefs/pref_service.h"
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
    net::DefineNetworkTrafficAnnotation("uw_chatgpt_plan_sign_in", R"(
      semantics {
        sender: "uw AI providers"
        description:
          "Signs a ChatGPT account in for the Codex provider: trades the "
          "code OpenAI's sign-in page gives for tokens, refreshes them, and "
          "revokes them on sign-out."
        trigger: "The user clicks Continue with ChatGPT or Sign out, or a "
          "request to Codex needs a fresh token."
        data: "The sign-in code, uw's client ID for the account, and tokens."
        destination: OTHER
        destination_other: "OpenAI's sign-in server, auth.openai.com."
      }
      policy {
        cookies_allowed: NO
        setting: "Nothing is sent until the user adds the Codex provider."
        policy_exception_justification: "Not implemented."
      })");

constexpr size_t kMaxAnswer = 256 * 1024;
// A token with less left than this is refreshed before it is used.
constexpr base::TimeDelta kRefreshAhead = base::Minutes(1);

constexpr char kSignedOut[] =
    "Codex is signed out. Continue with ChatGPT on the AI page to sign in.";
constexpr char kUnreachable[] =
    "uw couldn't renew the ChatGPT sign-in. Check the connection and try "
    "again.";

std::unique_ptr<network::SimpleURLLoader> Request(const GURL& url,
                                                  std::string form) {
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = url;
  resource->method = form.empty() ? "GET" : "POST";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->headers.SetHeader("Accept", "application/json");
  auto loader =
      network::SimpleURLLoader::Create(std::move(resource), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  if (!form.empty()) {
    loader->AttachStringForUpload(std::move(form),
                                  "application/x-www-form-urlencoded");
  }
  return loader;
}

int Status(const network::SimpleURLLoader& loader) {
  const network::mojom::URLResponseHead* head = loader.ResponseInfo();
  return head && head->headers ? head->headers->response_code() : 0;
}

PlanAddresses& Addresses() {
  static base::NoDestructor<PlanAddresses> addresses(
      PlanAddresses{GURL(chatgpt::kIssuer), chatgpt::kApi});
  return *addresses;
}

oauth::Client ClientOf(const std::string& id) {
  oauth::Client client;
  client.id = id;
  return client;
}

}  // namespace

// static
const PlanAddresses& PlanAddresses::Get() {
  return Addresses();
}

// static
void PlanAddresses::SetForTesting(PlanAddresses addresses) {
  Addresses() = std::move(addresses);
}

std::optional<chatgpt::Session> ReadPlanSession(const std::string& account) {
  if (account.empty()) {
    return std::nullopt;
  }
  const std::optional<std::string> json = ApiKeyStore().GetKey(account);
  return json ? chatgpt::SessionFromJson(*json) : std::nullopt;
}

bool SavePlanSession(const std::string& account,
                     const chatgpt::Session& session) {
  return ApiKeyStore().SetKey(account, chatgpt::SessionToJson(session));
}

PlanSignIn::PlanSignIn(std::optional<chatgpt::Session> previous,
                       PrefService* local_state,
                       scoped_refptr<network::SharedURLLoaderFactory> loader,
                       OpenTab open,
                       Done done)
    : previous_(std::move(previous)),
      loader_(std::move(loader)),
      done_(std::move(done)) {
  std::string host_id = local_state->GetString(kPlanHostIdPref);
  if (host_id.empty()) {
    host_id = chatgpt::MakeHostId();
    local_state->SetString(kPlanHostIdPref, host_id);
  }
  pkce_ = oauth::MakePkce();
  state_ = oauth::MakeState();
  nonce_ = oauth::MakeState();
  chatgpt::SignInRequest request;
  request.redirect_uri = oauth::kRedirectUri;
  request.host_id = host_id;
  request.challenge = pkce_.challenge;
  request.state = state_;
  request.nonce = nonce_;
  if (previous_) {
    client_id_ = previous_->client_id;
    request.client_id = previous_->client_id;
    request.id_token_hint = previous_->id_token;
    request.login_hint = previous_->email;
  }
  content::WebContents* tab = std::move(open).Run(
      chatgpt::AuthorizationUrl(PlanAddresses::Get().issuer, request));
  if (!tab) {
    // A sign-in that never started answers once its maker has it.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PlanSignIn::Fail, weak_factory_.GetWeakPtr(),
                                  PlanSignInError::Kind::kOther,
                                  "uw couldn't open OpenAI's sign-in page."));
    return;
  }
  Observe(tab);
}

PlanSignIn::~PlanSignIn() {
  if (content::WebContents* tab = web_contents()) {
    Observe(nullptr);
    tab->Close();
  }
}

void PlanSignIn::DidStartNavigation(content::NavigationHandle* navigation) {
  Caught(navigation->GetURL());
}

void PlanSignIn::DidRedirectNavigation(content::NavigationHandle* navigation) {
  Caught(navigation->GetURL());
}

void PlanSignIn::WebContentsDestroyed() {
  Observe(nullptr);
  if (done_ && !fetch_) {
    Fail(PlanSignInError::Kind::kOther, "You closed the sign-in tab.");
  }
}

void PlanSignIn::Caught(const GURL& url) {
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
  if (callback->state != state_) {
    Fail(PlanSignInError::Kind::kOther,
         "OpenAI's page sent back the wrong state.");
    return;
  }
  if (callback->error == "access_denied") {
    Fail(PlanSignInError::Kind::kDeclined,
         "You didn't let uw use your ChatGPT plan, so Codex wasn't added.");
    return;
  }
  if (!callback->error.empty()) {
    Fail(PlanSignInError::Kind::kOther,
         base::StrCat({"The sign-in didn't finish: ", callback->error}));
    return;
  }
  if (!callback->issuer.empty() &&
      GURL(callback->issuer) != PlanAddresses::Get().issuer) {
    Fail(PlanSignInError::Kind::kOther,
         "The sign-in came back from a server uw didn't ask.");
    return;
  }
  if (client_id_.empty()) {
    client_id_ = callback->client_id;
  } else if (!callback->client_id.empty() &&
             callback->client_id != client_id_) {
    client_id_.clear();
  }
  if (client_id_.empty()) {
    Fail(PlanSignInError::Kind::kOther, "OpenAI didn't register uw.");
    return;
  }
  fetch_ = Request(chatgpt::TokenUrl(PlanAddresses::Get().issuer),
                   oauth::CodeBody(callback->code, pkce_.verifier,
                                   ClientOf(client_id_), GURL(chatgpt::kApi)));
  fetch_->DownloadToString(
      loader_.get(),
      base::BindOnce(&PlanSignIn::OnTokens, weak_factory_.GetWeakPtr()),
      kMaxAnswer);
}

void PlanSignIn::OnTokens(std::optional<std::string> body) {
  const int status = Status(*fetch_);
  fetch_.reset();
  chatgpt::Session session;
  session.client_id = client_id_;
  const base::Time now = base::Time::Now();
  const chatgpt::TokenAnswer answer =
      status == 200 ? chatgpt::ReadTokens(body.value_or(""), now, session)
                    : chatgpt::TokenAnswer::kUnreadable;
  if (answer == chatgpt::TokenAnswer::kNoPlan) {
    Fail(PlanSignInError::Kind::kNoPlan, "");
    return;
  }
  std::optional<chatgpt::IdClaims> claims =
      answer == chatgpt::TokenAnswer::kOk
          ? chatgpt::CheckIdToken(session.id_token, PlanAddresses::Get().issuer,
                                  client_id_, nonce_, now)
          : std::nullopt;
  if (!claims) {
    Fail(PlanSignInError::Kind::kOther,
         "OpenAI's sign-in didn't give uw tokens it could use.");
    return;
  }
  if (previous_ && !previous_->subject.empty() &&
      previous_->subject != claims->subject) {
    Fail(PlanSignInError::Kind::kOther,
         "That's a different ChatGPT account. Add it as another Codex "
         "provider.");
    return;
  }
  session.email = claims->email;
  session.subject = claims->subject;
  if (done_) {
    std::move(done_).Run(std::move(session));
  }
}

void PlanSignIn::Fail(PlanSignInError::Kind kind, std::string message) {
  Observe(nullptr);
  if (done_) {
    std::move(done_).Run(
        base::unexpected(PlanSignInError{kind, std::move(message)}));
  }
}

// static
PlanTokens& PlanTokens::Get() {
  static base::NoDestructor<PlanTokens> tokens;
  return *tokens;
}

PlanTokens::PlanTokens() = default;
PlanTokens::~PlanTokens() = default;

void PlanTokens::Access(const std::string& account,
                        scoped_refptr<network::SharedURLLoaderFactory> loader,
                        Fresh done) {
  const std::optional<chatgpt::Session> session = ReadPlanSession(account);
  if (!session || !session->HasTokens()) {
    std::move(done).Run(base::unexpected(kSignedOut));
    return;
  }
  if (session->expires - base::Time::Now() > kRefreshAhead) {
    std::move(done).Run(base::ok(session->access));
    return;
  }
  std::vector<Fresh>& waiting = waiting_[account];
  waiting.push_back(std::move(done));
  if (waiting.size() > 1) {
    return;
  }
  std::unique_ptr<network::SimpleURLLoader> fetch =
      Request(chatgpt::TokenUrl(PlanAddresses::Get().issuer),
              oauth::RefreshBody(session->refresh, ClientOf(session->client_id),
                                 GURL(chatgpt::kApi)));
  network::SimpleURLLoader* raw = fetch.get();
  raw->DownloadToString(
      loader.get(),
      base::BindOnce(&PlanTokens::OnRefreshed, weak_factory_.GetWeakPtr(),
                     account, std::move(fetch)),
      kMaxAnswer);
}

void PlanTokens::OnRefreshed(std::string account,
                             std::unique_ptr<network::SimpleURLLoader> fetch,
                             std::optional<std::string> body) {
  std::optional<chatgpt::Session> session = ReadPlanSession(account);
  if (!session) {
    Answer(account, base::unexpected(kSignedOut));
    return;
  }
  const int status = Status(*fetch);
  if (status == 200 &&
      chatgpt::ReadTokens(body.value_or(""), base::Time::Now(), *session) ==
          chatgpt::TokenAnswer::kOk) {
    SavePlanSession(account, *session);
    Answer(account, base::ok(session->access));
    return;
  }
  if (status / 100 == 4 && chatgpt::IsTerminalRefreshError(body.value_or(""))) {
    session->access.clear();
    session->refresh.clear();
    SavePlanSession(account, *session);
    Answer(account, base::unexpected(kSignedOut));
    return;
  }
  Answer(account, base::unexpected(kUnreachable));
}

void PlanTokens::Answer(const std::string& account,
                        base::expected<std::string, std::string> result) {
  std::vector<Fresh> waiting = std::move(waiting_[account]);
  waiting_.erase(account);
  for (Fresh& done : waiting) {
    std::move(done).Run(result);
  }
}

void PlanTokens::SignOut(
    const std::string& account,
    scoped_refptr<network::SharedURLLoaderFactory> loader) {
  std::optional<chatgpt::Session> session = ReadPlanSession(account);
  if (!session) {
    return;
  }
  const std::string refresh = session->refresh;
  const std::string client_id = session->client_id;
  session->access.clear();
  session->refresh.clear();
  session->id_token.clear();
  SavePlanSession(account, *session);
  if (refresh.empty()) {
    return;
  }
  // OpenAI names its revocation address in its configuration. Whether it
  // answers or not, the tokens are gone from this Mac.
  std::unique_ptr<network::SimpleURLLoader> configuration =
      Request(chatgpt::ConfigurationUrl(PlanAddresses::Get().issuer), "");
  network::SimpleURLLoader* raw = configuration.get();
  raw->DownloadToString(
      loader.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> configuration,
             scoped_refptr<network::SharedURLLoaderFactory> loader,
             std::string refresh, std::string client_id,
             std::optional<std::string> body) {
            std::optional<base::DictValue> dict = base::JSONReader::ReadDict(
                body.value_or(""), base::JSON_PARSE_RFC);
            const std::string* endpoint =
                dict ? dict->FindString("revocation_endpoint") : nullptr;
            if (!endpoint || !GURL(*endpoint).SchemeIsHTTPOrHTTPS()) {
              return;
            }
            std::unique_ptr<network::SimpleURLLoader> revoke = Request(
                GURL(*endpoint),
                base::StrCat(
                    {"token=", base::EscapeQueryParamValue(refresh, true),
                     "&token_type_hint=refresh_token&client_id=",
                     base::EscapeQueryParamValue(client_id, true)}));
            network::SimpleURLLoader* sender = revoke.get();
            sender->DownloadToString(
                loader.get(),
                base::BindOnce([](std::unique_ptr<network::SimpleURLLoader>,
                                  std::optional<std::string>) {},
                               std::move(revoke)),
                kMaxAnswer);
          },
          std::move(configuration), loader, refresh, client_id),
      kMaxAnswer);
}

}  // namespace uw
