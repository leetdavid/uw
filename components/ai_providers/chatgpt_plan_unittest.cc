// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/chatgpt_plan.h"

#include <optional>
#include <string>

#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "net/base/url_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw::chatgpt {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;

std::string Query(const GURL& url, std::string_view key) {
  std::string value;
  net::GetValueForKeyInQuery(url, std::string(key), &value);
  return value;
}

std::string IdToken(std::string_view payload) {
  std::string encoded;
  base::Base64UrlEncode(payload, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &encoded);
  return "eyJhbGciOiJSUzI1NiJ9." + encoded + ".signature";
}

TEST(ChatGptPlanTest, AFirstSignInRegistersUw) {
  SignInRequest request;
  request.redirect_uri = "http://127.0.0.1:47651/callback";
  request.host_id = "urn:uuid:4f1c";
  request.challenge = "abc";
  request.state = "st";
  request.nonce = "no";
  const GURL url = AuthorizationUrl(GURL(kIssuer), request);
  EXPECT_EQ(url.path(), "/api/accounts/authorize");
  EXPECT_EQ(Query(url, "client_id"), "dynamic_agent_client");
  EXPECT_EQ(Query(url, "agent_name_hint"), "uw");
  EXPECT_EQ(Query(url, "ext_agent_host_id"), "urn:uuid:4f1c");
  EXPECT_EQ(Query(url, "redirect_uri"), "http://127.0.0.1:47651/callback");
  EXPECT_EQ(Query(url, "scope"),
            "openid profile email offline_access resource.invoke "
            "chatgpt.tokens.use.direct");
  EXPECT_EQ(Query(url, "resource"), "https://api.openai.com/v1");
  EXPECT_EQ(Query(url, "code_challenge_method"), "S256");
  EXPECT_EQ(Query(url, "id_token_hint"), "");
}

TEST(ChatGptPlanTest, ASecondSignInReusesItsClient) {
  SignInRequest request;
  request.client_id = "oaiapp_1";
  request.host_id = "urn:uuid:4f1c";
  request.id_token_hint = "old.id.token";
  request.login_hint = "ada@example.com";
  const GURL url = AuthorizationUrl(GURL(kIssuer), request);
  EXPECT_EQ(Query(url, "client_id"), "oaiapp_1");
  EXPECT_EQ(Query(url, "agent_name_hint"), "");
  EXPECT_EQ(Query(url, "id_token_hint"), "old.id.token");
  EXPECT_EQ(Query(url, "login_hint"), "ada@example.com");
}

TEST(ChatGptPlanTest, TokensNeedThePlanScope) {
  const base::Time now = base::Time::FromSecondsSinceUnixEpoch(1000);
  Session session;
  EXPECT_EQ(ReadTokens(R"({"access_token":"a1","refresh_token":"r1",)"
                       R"("id_token":"i1","expires_in":3600,)"
                       R"("scope":"openid email chatgpt.tokens.use.direct"})",
                       now, session),
            TokenAnswer::kOk);
  EXPECT_EQ(session.access, "a1");
  EXPECT_EQ(session.refresh, "r1");
  EXPECT_EQ(session.expires, now + base::Hours(1));
  // A refresh replaces both tokens and may leave the ID token out.
  EXPECT_EQ(ReadTokens(R"({"access_token":"a2","refresh_token":"r2",)"
                       R"("expires_in":3600})",
                       now, session),
            TokenAnswer::kOk);
  EXPECT_EQ(session.refresh, "r2");
  EXPECT_EQ(session.id_token, "i1");

  Session free;
  EXPECT_EQ(
      ReadTokens(R"({"access_token":"a","scope":"openid email"})", now, free),
      TokenAnswer::kNoPlan);
  EXPECT_FALSE(free.HasTokens());
  EXPECT_EQ(ReadTokens("{}", now, free), TokenAnswer::kUnreadable);
}

TEST(ChatGptPlanTest, OnlySomeRefreshErrorsEndTheSignIn) {
  EXPECT_TRUE(IsTerminalRefreshError(R"({"error":"invalid_grant"})"));
  EXPECT_TRUE(
      IsTerminalRefreshError(R"({"error":{"code":"refresh_token_reused"}})"));
  EXPECT_FALSE(
      IsTerminalRefreshError(R"({"error":"temporarily_unavailable"})"));
  EXPECT_FALSE(IsTerminalRefreshError("<html>"));
}

TEST(ChatGptPlanTest, TheIdTokenMustMatchTheSignIn) {
  const base::Time now = base::Time::FromSecondsSinceUnixEpoch(1000);
  const std::string token = IdToken(
      R"({"iss":"https://auth.openai.com","aud":["oaiapp_1"],"sub":"user-1",)"
      R"("email":"ada@example.com","nonce":"n1","exp":2000})");
  const std::optional<IdClaims> claims =
      CheckIdToken(token, GURL(kIssuer), "oaiapp_1", "n1", now);
  ASSERT_TRUE(claims);
  EXPECT_EQ(claims->email, "ada@example.com");
  EXPECT_EQ(claims->subject, "user-1");
  EXPECT_FALSE(CheckIdToken(token, GURL(kIssuer), "oaiapp_2", "n1", now));
  EXPECT_FALSE(CheckIdToken(token, GURL(kIssuer), "oaiapp_1", "n2", now));
  EXPECT_FALSE(CheckIdToken(token, GURL("https://auth.example.com"), "oaiapp_1",
                            "n1", now));
  EXPECT_FALSE(CheckIdToken(token, GURL(kIssuer), "oaiapp_1", "n1",
                            base::Time::FromSecondsSinceUnixEpoch(3000)));
  EXPECT_FALSE(
      CheckIdToken("not a token", GURL(kIssuer), "oaiapp_1", "n1", now));
}

TEST(ChatGptPlanTest, APlanRequestIsStreamedWithoutRefusedFields) {
  const std::optional<base::DictValue> request = base::JSONReader::ReadDict(
      ForPlan(R"({"model":"gpt-6.1-sol","store":false,"max_output_tokens":64,)"
              R"("temperature":0.2,"input":[],"instructions":"Be brief."})"),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(request);
  EXPECT_EQ(request->FindBool("stream"), true);
  EXPECT_EQ(request->FindBool("store"), false);
  EXPECT_FALSE(request->Find("max_output_tokens"));
  EXPECT_FALSE(request->Find("temperature"));
  EXPECT_EQ(*request->FindString("instructions"), "Be brief.");
}

TEST(ChatGptPlanTest, AStreamEndsInTheCompletedResponse) {
  const StreamEnd end = ReadStream(
      "event: response.created\n"
      "data: {\"type\":\"response.created\",\"response\":{\"status\":"
      "\"in_progress\"}}\n\n"
      "event: response.output_text.delta\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hel\"}\n\n"
      "event: response.completed\r\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":"
      "\"completed\",\"output\":[]}}\r\n\r\n");
  ASSERT_TRUE(end.response);
  EXPECT_EQ(*end.response, R"({"output":[],"status":"completed"})");
}

TEST(ChatGptPlanTest, AStreamCanEndInAnError) {
  const StreamEnd limit = ReadStream(
      "data: {\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":"
      "\"subscription_sharing_usage_limit_exceeded\",\"message\":\"Limit "
      "reached.\"}}}\n\n");
  EXPECT_FALSE(limit.response);
  EXPECT_EQ(limit.error_code, "subscription_sharing_usage_limit_exceeded");
  EXPECT_EQ(ErrorSentence(limit.error_code, limit.error_message),
            "Limit reached. See https://chatgpt.com/settings/usage.");
  EXPECT_EQ(ErrorStatus(limit.error_code), 429);
  EXPECT_EQ(ErrorStatus("subscription_sharing_usage_unavailable"), 503);

  const StreamEnd cut = ReadStream(
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hel\"}\n\n");
  EXPECT_EQ(cut.error_code, "stream_ended");
  EXPECT_EQ(ErrorSentence(cut.error_code, cut.error_message),
            "OpenAI's answer stopped before it was complete.");
}

TEST(ChatGptPlanTest, ThePlanListsModelsForAPicker) {
  const std::optional<std::vector<PlanModel>> models = ReadModels(
      R"({"models":[)"
      R"({"slug":"gpt-6.1-sol","display_name":"GPT-6.1 Sol","visibility":"list"},)"
      R"({"slug":"codex-auto-review","display_name":"Review","visibility":"hide"},)"
      R"({"slug":"gpt-6-luna","visibility":"list"}]})");
  ASSERT_TRUE(models);
  EXPECT_THAT(*models, ElementsAre(Field(&PlanModel::name, "GPT-6.1 Sol"),
                                   Field(&PlanModel::name, "gpt-6-luna")));
  EXPECT_FALSE(ReadModels(R"({"data":[]})"));
}

TEST(ChatGptPlanTest, ASessionReadsBackTheSame) {
  Session session;
  session.client_id = "oaiapp_1";
  session.email = "ada@example.com";
  session.access = "a1";
  session.expires = base::Time::FromSecondsSinceUnixEpoch(5000);
  const std::optional<Session> read = SessionFromJson(SessionToJson(session));
  ASSERT_TRUE(read);
  EXPECT_EQ(read->email, "ada@example.com");
  EXPECT_EQ(read->expires, session.expires);
  EXPECT_TRUE(read->HasTokens());
  EXPECT_FALSE(SessionFromJson("{}"));
}

}  // namespace
}  // namespace uw::chatgpt
