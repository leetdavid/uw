// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/oauth.h"

#include <utility>

#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "net/base/url_util.h"

namespace uw::oauth {
namespace {

std::string Base64Url(std::string_view bytes) {
  std::string text;
  base::Base64UrlEncode(bytes, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &text);
  return text;
}

GURL WithPath(const GURL& url, std::string_view path) {
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  replacements.ClearQuery();
  replacements.ClearRef();
  return url.ReplaceComponents(replacements);
}

// A URL's path without its slashes at either end.
std::string_view InnerPath(const GURL& url) {
  return base::TrimString(url.path(), "/", base::TRIM_ALL);
}

std::string Form(
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        fields) {
  std::string body;
  for (const auto& [key, value] : fields) {
    if (value.empty()) {
      continue;
    }
    base::StrAppend(&body, {body.empty() ? "" : "&", key, "=",
                            base::EscapeQueryParamValue(value, true)});
  }
  return body;
}

GURL ReadUrl(const base::DictValue& dict, std::string_view key) {
  const std::string* text = dict.FindString(key);
  return text ? GURL(*text) : GURL();
}

}  // namespace

ResourceMetadata::ResourceMetadata() = default;
ResourceMetadata::ResourceMetadata(const ResourceMetadata&) = default;
ResourceMetadata& ResourceMetadata::operator=(const ResourceMetadata&) =
    default;
ResourceMetadata::~ResourceMetadata() = default;

Tokens::Tokens() = default;
Tokens::Tokens(const Tokens&) = default;
Tokens& Tokens::operator=(const Tokens&) = default;
Tokens::~Tokens() = default;

std::vector<GURL> ResourceMetadataUrls(const GURL& server) {
  constexpr std::string_view kWellKnown =
      "/.well-known/oauth-protected-resource";
  std::vector<GURL> urls;
  if (const std::string_view path = InnerPath(server); !path.empty()) {
    urls.push_back(WithPath(server, base::StrCat({kWellKnown, "/", path})));
  }
  urls.push_back(WithPath(server, kWellKnown));
  return urls;
}

std::vector<GURL> ServerMetadataUrls(const GURL& issuer) {
  const std::string_view path = InnerPath(issuer);
  if (path.empty()) {
    return {WithPath(issuer, "/.well-known/oauth-authorization-server"),
            WithPath(issuer, "/.well-known/openid-configuration")};
  }
  return {
      WithPath(issuer, base::StrCat(
                           {"/.well-known/oauth-authorization-server/", path})),
      WithPath(issuer,
               base::StrCat({"/.well-known/openid-configuration/", path})),
      WithPath(issuer,
               base::StrCat({"/", path, "/.well-known/openid-configuration"})),
  };
}

std::optional<ResourceMetadata> ParseResourceMetadata(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* servers =
      dict ? dict->FindList("authorization_servers") : nullptr;
  if (!servers) {
    return std::nullopt;
  }
  ResourceMetadata metadata;
  for (const base::Value& server : *servers) {
    if (server.is_string() && GURL(server.GetString()).is_valid()) {
      metadata.authorization_servers.emplace_back(server.GetString());
    }
  }
  if (const base::ListValue* scopes = dict->FindList("scopes_supported")) {
    for (const base::Value& scope : *scopes) {
      if (scope.is_string()) {
        metadata.scopes.push_back(scope.GetString());
      }
    }
  }
  if (metadata.authorization_servers.empty()) {
    return std::nullopt;
  }
  return metadata;
}

std::optional<ServerMetadata> ParseServerMetadata(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  ServerMetadata server;
  if (const std::string* issuer = dict->FindString("issuer")) {
    server.issuer = *issuer;
  }
  server.authorization_endpoint = ReadUrl(*dict, "authorization_endpoint");
  server.token_endpoint = ReadUrl(*dict, "token_endpoint");
  server.registration_endpoint = ReadUrl(*dict, "registration_endpoint");
  if (!server.authorization_endpoint.is_valid() ||
      !server.token_endpoint.is_valid()) {
    return std::nullopt;
  }
  if (const base::ListValue* methods =
          dict->FindList("code_challenge_methods_supported")) {
    for (const base::Value& method : *methods) {
      server.s256 |= method.is_string() && method.GetString() == "S256";
    }
  }
  return server;
}

std::string RegistrationBody() {
  const auto list = [](std::initializer_list<std::string_view> items) {
    base::ListValue values;
    for (std::string_view item : items) {
      values.Append(item);
    }
    return values;
  };
  return base::WriteJson(base::DictValue()
                             .Set("client_name", "uw")
                             .Set("redirect_uris", list({kRedirectUri}))
                             .Set("grant_types",
                                  list({"authorization_code", "refresh_token"}))
                             .Set("response_types", list({"code"}))
                             .Set("token_endpoint_auth_method", "none")
                             .Set("application_type", "native"))
      .value_or("");
}

std::optional<Client> ParseClient(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const std::string* id = dict ? dict->FindString("client_id") : nullptr;
  if (!id || id->empty()) {
    return std::nullopt;
  }
  Client client;
  client.id = *id;
  if (const std::string* secret = dict->FindString("client_secret")) {
    client.secret = *secret;
  }
  return client;
}

Pkce MakePkce() {
  Pkce pkce;
  pkce.verifier = Base64Url(base::RandBytesAsString(32));
  pkce.challenge = Challenge(pkce.verifier);
  return pkce;
}

std::string Challenge(std::string_view verifier) {
  return Base64Url(crypto::SHA256HashString(verifier));
}

std::string MakeState() {
  return Base64Url(base::RandBytesAsString(16));
}

GURL AuthorizationUrl(const ServerMetadata& server,
                      const Client& client,
                      std::string_view challenge,
                      std::string_view state,
                      const GURL& resource,
                      const std::vector<std::string>& scopes) {
  GURL url = server.authorization_endpoint;
  url = net::AppendQueryParameter(url, "response_type", "code");
  url = net::AppendQueryParameter(url, "client_id", client.id);
  url = net::AppendQueryParameter(url, "redirect_uri", kRedirectUri);
  url = net::AppendQueryParameter(url, "code_challenge", challenge);
  url = net::AppendQueryParameter(url, "code_challenge_method", "S256");
  url = net::AppendQueryParameter(url, "state", state);
  url = net::AppendQueryParameter(url, "resource", resource.spec());
  if (!scopes.empty()) {
    url =
        net::AppendQueryParameter(url, "scope", base::JoinString(scopes, " "));
  }
  return url;
}

std::optional<Callback> ParseCallback(const GURL& url) {
  const GURL redirect(kRedirectUri);
  if (url.scheme() != redirect.scheme() || url.host() != redirect.host() ||
      url.EffectiveIntPort() != redirect.EffectiveIntPort() ||
      url.path() != redirect.path()) {
    return std::nullopt;
  }
  Callback callback;
  net::GetValueForKeyInQuery(url, "code", &callback.code);
  net::GetValueForKeyInQuery(url, "state", &callback.state);
  net::GetValueForKeyInQuery(url, "iss", &callback.issuer);
  net::GetValueForKeyInQuery(url, "error", &callback.error);
  if (callback.code.empty() && callback.error.empty()) {
    callback.error = "no code";
  }
  return callback;
}

std::string CodeBody(std::string_view code,
                     std::string_view verifier,
                     const Client& client,
                     const GURL& resource) {
  return Form({{"grant_type", "authorization_code"},
               {"code", code},
               {"redirect_uri", kRedirectUri},
               {"client_id", client.id},
               {"client_secret", client.secret},
               {"code_verifier", verifier},
               {"resource", resource.spec()}});
}

std::string RefreshBody(std::string_view refresh_token,
                        const Client& client,
                        const GURL& resource) {
  return Form({{"grant_type", "refresh_token"},
               {"refresh_token", refresh_token},
               {"client_id", client.id},
               {"client_secret", client.secret},
               {"resource", resource.spec()}});
}

bool ParseTokens(std::string_view json, base::Time now, Tokens& tokens) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const std::string* access = dict ? dict->FindString("access_token") : nullptr;
  if (!access || access->empty()) {
    return false;
  }
  tokens.access = *access;
  if (const std::string* refresh = dict->FindString("refresh_token")) {
    tokens.refresh = *refresh;
  }
  const std::optional<int> lifetime = dict->FindInt("expires_in");
  tokens.expires = lifetime ? now + base::Seconds(*lifetime) : base::Time();
  return true;
}

std::string TokensToJson(const Tokens& tokens) {
  return base::WriteJson(
             base::DictValue()
                 .Set("access", tokens.access)
                 .Set("refresh", tokens.refresh)
                 .Set(
                     "expires",
                     tokens.expires.InMillisecondsFSinceUnixEpochIgnoringNull())
                 .Set("token_endpoint", tokens.token_endpoint.spec())
                 .Set("client_id", tokens.client.id)
                 .Set("client_secret", tokens.client.secret))
      .value_or("");
}

std::optional<Tokens> TokensFromJson(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const std::string* access = dict ? dict->FindString("access") : nullptr;
  if (!access) {
    return std::nullopt;
  }
  Tokens tokens;
  tokens.access = *access;
  const auto text = [&](std::string_view key) {
    const std::string* value = dict->FindString(key);
    return value ? *value : std::string();
  };
  tokens.refresh = text("refresh");
  tokens.token_endpoint = GURL(text("token_endpoint"));
  tokens.client.id = text("client_id");
  tokens.client.secret = text("client_secret");
  if (const std::optional<double> expires = dict->FindDouble("expires");
      expires && *expires > 0) {
    tokens.expires = base::Time::FromMillisecondsSinceUnixEpoch(*expires);
  }
  return tokens;
}

}  // namespace uw::oauth
