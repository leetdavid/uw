// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_PLAN_SIGN_IN_H_
#define UW_BROWSER_AI_PROVIDERS_PLAN_SIGN_IN_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"
#include "uw/components/ai_providers/chatgpt_plan.h"
#include "uw/components/connectors/oauth.h"

class PrefService;

namespace content {
class WebContents;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// Local state: this Mac's own ID for OpenAI's sign-in, made at the first.
inline constexpr char kPlanHostIdPref[] = "uw.providers.plan_host_id";

// Where the Codex preset signs in and sends its requests. Tests point both
// at a local server.
struct PlanAddresses {
  GURL issuer;
  std::string api;

  static const PlanAddresses& Get();
  static void SetForTesting(PlanAddresses addresses);
};

// Why a sign-in ended without a session.
struct PlanSignInError {
  enum class Kind { kDeclined, kNoPlan, kOther };
  Kind kind = Kind::kOther;
  std::string message;
};

// Signs a ChatGPT account in for the Codex preset, by OpenAI's flow for
// open-source apps: opens OpenAI's page in a tab, catches the address it
// sends back to, trades the code for tokens, and checks them. See
// docs/plans/ai-providers.md#subscriptions.
class PlanSignIn : public content::WebContentsObserver {
 public:
  using Done = base::OnceCallback<void(
      base::expected<chatgpt::Session, PlanSignInError>)>;
  // Opens OpenAI's page and returns its tab.
  using OpenTab = base::OnceCallback<content::WebContents*(const GURL&)>;

  // `previous` is the account's last sign-in, whose client ID it reuses.
  PlanSignIn(std::optional<chatgpt::Session> previous,
             PrefService* local_state,
             scoped_refptr<network::SharedURLLoaderFactory> loader,
             OpenTab open,
             Done done);
  PlanSignIn(const PlanSignIn&) = delete;
  PlanSignIn& operator=(const PlanSignIn&) = delete;
  // Closes the sign-in tab if it is still open.
  ~PlanSignIn() override;

 private:
  void Caught(const GURL& url);
  void OnTokens(std::optional<std::string> body);
  void Fail(PlanSignInError::Kind kind, std::string message);

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void DidRedirectNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;

  const std::optional<chatgpt::Session> previous_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  Done done_;
  std::unique_ptr<network::SimpleURLLoader> fetch_;
  oauth::Pkce pkce_;
  std::string state_;
  std::string nonce_;
  std::string client_id_;
  base::WeakPtrFactory<PlanSignIn> weak_factory_{this};
};

// The Codex preset's tokens, kept in Keychain as a chatgpt::Session under
// the provider's key account. Hands out fresh access tokens, refreshing one
// that is about to expire, one refresh per account at a time.
class PlanTokens {
 public:
  // The access token, or the sentence that says why there is none.
  using Fresh =
      base::OnceCallback<void(base::expected<std::string, std::string>)>;

  static PlanTokens& Get();

  PlanTokens();
  PlanTokens(const PlanTokens&) = delete;
  PlanTokens& operator=(const PlanTokens&) = delete;
  ~PlanTokens();

  void Access(const std::string& account,
              scoped_refptr<network::SharedURLLoaderFactory> loader,
              Fresh done);
  // Drops the account's tokens at once, keeping its client ID and email for
  // the next sign-in, then asks OpenAI to revoke the refresh token.
  void SignOut(const std::string& account,
               scoped_refptr<network::SharedURLLoaderFactory> loader);

 private:
  void OnRefreshed(std::string account,
                   std::unique_ptr<network::SimpleURLLoader> fetch,
                   std::optional<std::string> body);
  void Answer(const std::string& account,
              base::expected<std::string, std::string> result);

  std::map<std::string, std::vector<Fresh>> waiting_;
  base::WeakPtrFactory<PlanTokens> weak_factory_{this};
};

// The session saved under `account`, if any.
std::optional<chatgpt::Session> ReadPlanSession(const std::string& account);
bool SavePlanSession(const std::string& account,
                     const chatgpt::Session& session);

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_PLAN_SIGN_IN_H_
