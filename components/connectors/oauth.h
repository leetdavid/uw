// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_CONNECTORS_OAUTH_H_
#define UW_COMPONENTS_CONNECTORS_OAUTH_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "url/gurl.h"

// How a web connector signs in, by MCP's authorization rules: OAuth with
// automatic client registration and PKCE. See docs/plans/connectors.md.
namespace uw::oauth {

// Where the sign-in page sends the browser back. uw catches it in the tab,
// so nothing listens there.
inline constexpr char kRedirectUri[] = "http://127.0.0.1:47651/callback";

// Where a server's protected-resource metadata may be, most specific first,
// when its 401 did not say.
std::vector<GURL> ResourceMetadataUrls(const GURL& server);
// Where an issuer's metadata may be, in the order MCP asks clients to try.
std::vector<GURL> ServerMetadataUrls(const GURL& issuer);

struct ResourceMetadata {
  ResourceMetadata();
  ResourceMetadata(const ResourceMetadata&);
  ResourceMetadata& operator=(const ResourceMetadata&);
  ~ResourceMetadata();

  std::vector<GURL> authorization_servers;
  std::vector<std::string> scopes;
};
std::optional<ResourceMetadata> ParseResourceMetadata(std::string_view json);

struct ServerMetadata {
  std::string issuer;
  GURL authorization_endpoint;
  GURL token_endpoint;
  GURL registration_endpoint;
  // PKCE with S256, which MCP requires.
  bool s256 = false;
};
// Nothing for metadata without both endpoints.
std::optional<ServerMetadata> ParseServerMetadata(std::string_view json);

// What uw tells a server when it registers itself.
std::string RegistrationBody();
struct Client {
  std::string id;
  std::string secret;
};
std::optional<Client> ParseClient(std::string_view json);

struct Pkce {
  std::string verifier;
  std::string challenge;
};
Pkce MakePkce();
// The S256 challenge for `verifier`.
std::string Challenge(std::string_view verifier);
// Random text for the state parameter.
std::string MakeState();

GURL AuthorizationUrl(const ServerMetadata& server,
                      const Client& client,
                      std::string_view challenge,
                      std::string_view state,
                      const GURL& resource,
                      const std::vector<std::string>& scopes);

// What came back to kRedirectUri. Nothing for another address.
struct Callback {
  std::string code;
  std::string state;
  std::string issuer;
  // The server's error, such as "access_denied".
  std::string error;
};
std::optional<Callback> ParseCallback(const GURL& url);

// Form bodies for the token endpoint.
std::string CodeBody(std::string_view code,
                     std::string_view verifier,
                     const Client& client,
                     const GURL& resource);
std::string RefreshBody(std::string_view refresh_token,
                        const Client& client,
                        const GURL& resource);

// A sign-in as uw keeps it in Keychain.
struct Tokens {
  Tokens();
  Tokens(const Tokens&);
  Tokens& operator=(const Tokens&);
  ~Tokens();

  std::string access;
  std::string refresh;
  // Null when the server gave no lifetime.
  base::Time expires;
  // What a refresh needs.
  GURL token_endpoint;
  Client client;
};
// Fills `tokens` from a token endpoint's answer. False for an answer
// without an access token. A refresh may leave the refresh token as it was.
bool ParseTokens(std::string_view json, base::Time now, Tokens& tokens);
std::string TokensToJson(const Tokens& tokens);
std::optional<Tokens> TokensFromJson(std::string_view json);

}  // namespace uw::oauth

#endif  // UW_COMPONENTS_CONNECTORS_OAUTH_H_
