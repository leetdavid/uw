// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_SIGN_IN_H_
#define UW_BROWSER_CONNECTORS_SIGN_IN_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"
#include "uw/components/connectors/oauth.h"

namespace content {
class WebContents;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// Why a sign-in ended without tokens.
struct SignInError {
  std::string message;
  // The sign-in server takes no registration uw can make.
  bool cannot_register = false;
};

// Signs one web connector in, by MCP's authorization rules: finds its
// sign-in server, registers uw there unless it did before, opens the
// sign-in page in a tab, catches the address the page sends back to, and
// trades the code for tokens.
class ConnectorSignIn : public content::WebContentsObserver {
 public:
  using Done =
      base::OnceCallback<void(base::expected<oauth::Tokens, SignInError>)>;
  // Opens the sign-in page and returns its tab.
  using OpenTab = base::OnceCallback<content::WebContents*(const GURL&)>;

  // `resource_metadata` is where the connector's 401 said to look, if it
  // did.
  ConnectorSignIn(GURL server,
                  GURL resource_metadata,
                  scoped_refptr<network::SharedURLLoaderFactory> loader,
                  OpenTab open,
                  Done done);
  ConnectorSignIn(const ConnectorSignIn&) = delete;
  ConnectorSignIn& operator=(const ConnectorSignIn&) = delete;
  ~ConnectorSignIn() override;

  // Trades a refresh token for new tokens.
  static void Refresh(
      oauth::Tokens tokens,
      GURL server,
      scoped_refptr<network::SharedURLLoaderFactory> loader,
      base::OnceCallback<void(std::optional<oauth::Tokens>)> done);

 private:
  using Fetched = base::OnceCallback<void(int status, std::string body)>;

  void Fetch(const GURL& url,
             std::string form_or_json,
             std::string content_type,
             Fetched done);
  void OnFetched(Fetched done, std::optional<std::string> body);
  void TryResourceMetadata(std::vector<GURL> urls);
  void OnResourceMetadata(std::vector<GURL> urls, int status, std::string body);
  void TryServerMetadata(std::vector<GURL> urls);
  void OnServerMetadata(std::vector<GURL> urls, int status, std::string body);
  void OnRegistered(int status, std::string body);
  void OpenSignInPage(oauth::Client client);
  void Caught(const GURL& url);
  void OnTokens(int status, std::string body);
  void Finish(base::expected<oauth::Tokens, SignInError> result);

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void DidRedirectNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;

  const GURL server_;
  const GURL resource_metadata_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  OpenTab open_;
  Done done_;
  std::unique_ptr<network::SimpleURLLoader> fetch_;
  std::vector<std::string> scopes_;
  oauth::ServerMetadata metadata_;
  oauth::Client client_;
  oauth::Pkce pkce_;
  std::string state_;
  base::WeakPtrFactory<ConnectorSignIn> weak_factory_{this};
};

// The Keychain account that keeps the client uw registered with `issuer`,
// so a second connector at the same sign-in server reuses it.
std::string ClientAccount(std::string_view issuer);

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_SIGN_IN_H_
