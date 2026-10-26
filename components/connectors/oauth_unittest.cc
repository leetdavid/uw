// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/oauth.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw::oauth {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

TEST(OAuthTest, LooksForMetadataWhereMcpSays) {
  EXPECT_THAT(
      ResourceMetadataUrls(GURL("https://mcp.linear.app/mcp?x=1")),
      ElementsAre(
          GURL("https://mcp.linear.app/.well-known/oauth-protected-resource/"
               "mcp"),
          GURL("https://mcp.linear.app/.well-known/oauth-protected-resource")));
  EXPECT_THAT(ResourceMetadataUrls(GURL("https://doop.design/")),
              ElementsAre(GURL(
                  "https://doop.design/.well-known/oauth-protected-resource")));
  EXPECT_THAT(
      ServerMetadataUrls(GURL("https://auth.example.com")),
      ElementsAre(
          GURL("https://auth.example.com/.well-known/"
               "oauth-authorization-server"),
          GURL("https://auth.example.com/.well-known/openid-configuration")));
  EXPECT_THAT(ServerMetadataUrls(GURL("https://auth.example.com/tenant1")),
              ElementsAre(GURL("https://auth.example.com/.well-known/"
                               "oauth-authorization-server/tenant1"),
                          GURL("https://auth.example.com/.well-known/"
                               "openid-configuration/tenant1"),
                          GURL("https://auth.example.com/tenant1/.well-known/"
                               "openid-configuration")));
}

TEST(OAuthTest, ReadsMetadata) {
  std::optional<ResourceMetadata> resource = ParseResourceMetadata(R"({
    "resource": "https://mcp.linear.app/mcp",
    "authorization_servers": ["https://mcp.linear.app"],
    "scopes_supported": ["read", "write"]})");
  ASSERT_TRUE(resource);
  EXPECT_THAT(resource->authorization_servers,
              ElementsAre(GURL("https://mcp.linear.app")));
  EXPECT_THAT(resource->scopes, ElementsAre("read", "write"));
  EXPECT_FALSE(ParseResourceMetadata(R"({"authorization_servers": []})"));

  std::optional<ServerMetadata> server = ParseServerMetadata(R"({
    "issuer": "https://mcp.linear.app",
    "authorization_endpoint": "https://mcp.linear.app/authorize",
    "token_endpoint": "https://mcp.linear.app/token",
    "registration_endpoint": "https://mcp.linear.app/register",
    "code_challenge_methods_supported": ["plain", "S256"]})");
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->s256);
  EXPECT_EQ(server->registration_endpoint,
            GURL("https://mcp.linear.app/register"));
  EXPECT_FALSE(ParseServerMetadata(R"({"token_endpoint": "https://a/t"})"));
}

TEST(OAuthTest, MakesThePkceChallengeTheRfcDoes) {
  EXPECT_EQ(Challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
            "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  const Pkce pkce = MakePkce();
  EXPECT_EQ(pkce.verifier.size(), 43u);
  EXPECT_EQ(pkce.challenge, Challenge(pkce.verifier));
  EXPECT_NE(MakeState(), MakeState());
}

TEST(OAuthTest, BuildsTheRequests) {
  ServerMetadata server;
  server.authorization_endpoint = GURL("https://auth.test/authorize");
  Client client{.id = "uw-123"};
  const GURL url =
      AuthorizationUrl(server, client, "chal", "st",
                       GURL("https://mcp.test/mcp"), {"read", "write"});
  EXPECT_EQ(url.spec(),
            "https://auth.test/authorize?response_type=code&client_id=uw-123"
            "&redirect_uri=http%3A%2F%2F127.0.0.1%3A47651%2Fcallback"
            "&code_challenge=chal&code_challenge_method=S256&state=st"
            "&resource=https%3A%2F%2Fmcp.test%2Fmcp&scope=read+write");
  EXPECT_EQ(CodeBody("c 1", "v", client, GURL("https://mcp.test/mcp")),
            "grant_type=authorization_code&code=c+1"
            "&redirect_uri=http%3A%2F%2F127.0.0.1%3A47651%2Fcallback"
            "&client_id=uw-123&code_verifier=v"
            "&resource=https%3A%2F%2Fmcp.test%2Fmcp");
  EXPECT_THAT(RefreshBody("r", Client{.id = "a", .secret = "s"},
                          GURL("https://mcp.test/mcp")),
              HasSubstr("grant_type=refresh_token&refresh_token=r"
                        "&client_id=a&client_secret=s"));
  EXPECT_THAT(RegistrationBody(), HasSubstr("\"token_endpoint_auth_method\":"
                                            "\"none\""));
  EXPECT_EQ(ParseClient(R"({"client_id": "abc"})")->id, "abc");
  EXPECT_FALSE(ParseClient("{}"));
}

TEST(OAuthTest, ReadsWhatComesBack) {
  std::optional<Callback> back = ParseCallback(
      GURL("http://127.0.0.1:47651/callback?code=xyz&state=st&iss=https%3A%2F"
           "%2Fauth.test"));
  ASSERT_TRUE(back);
  EXPECT_EQ(back->code, "xyz");
  EXPECT_EQ(back->state, "st");
  EXPECT_EQ(back->issuer, "https://auth.test");
  EXPECT_EQ(ParseCallback(GURL("http://127.0.0.1:47651/callback?error="
                               "access_denied"))
                ->error,
            "access_denied");
  EXPECT_FALSE(ParseCallback(GURL("http://127.0.0.1:47652/callback?code=1")));
  EXPECT_FALSE(ParseCallback(GURL("https://auth.test/callback?code=1")));

  Tokens tokens;
  tokens.refresh = "old";
  const base::Time now = base::Time::FromMillisecondsSinceUnixEpoch(1000.0);
  ASSERT_TRUE(
      ParseTokens(R"({"access_token": "a1", "expires_in": 60})", now, tokens));
  EXPECT_EQ(tokens.access, "a1");
  EXPECT_EQ(tokens.refresh, "old");
  EXPECT_EQ(tokens.expires, now + base::Seconds(60));
  EXPECT_FALSE(ParseTokens(R"({"error": "invalid_grant"})", now, tokens));

  tokens.token_endpoint = GURL("https://auth.test/token");
  tokens.client.id = "uw-123";
  std::optional<Tokens> back_again = TokensFromJson(TokensToJson(tokens));
  ASSERT_TRUE(back_again);
  EXPECT_EQ(back_again->access, "a1");
  EXPECT_EQ(back_again->expires, tokens.expires);
  EXPECT_EQ(back_again->token_endpoint, tokens.token_endpoint);
  EXPECT_EQ(back_again->client.id, "uw-123");
}

}  // namespace
}  // namespace uw::oauth
