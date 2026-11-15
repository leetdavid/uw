// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AI_PROVIDERS_CHATGPT_PLAN_H_
#define UW_COMPONENTS_AI_PROVIDERS_CHATGPT_PLAN_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "url/gurl.h"

// The Codex preset: a ChatGPT Plus or Pro plan, reached through OpenAI's
// sign-in for open-source and locally hosted apps. See
// docs/plans/ai-providers.md#subscriptions and
// https://developers.openai.com/siwc/token-sharing-open-source.
namespace uw::chatgpt {

inline constexpr char kIssuer[] = "https://auth.openai.com";
inline constexpr char kApi[] = "https://api.openai.com/v1";
// The client ID of a first sign-in, which registers uw for the account.
inline constexpr char kFirstClientId[] = "dynamic_agent_client";
// The scope that lets uw spend the plan.
inline constexpr char kPlanScope[] = "chatgpt.tokens.use.direct";
// Where a usage limit sends the user.
inline constexpr char kUsagePage[] = "https://chatgpt.com/settings/usage";

GURL AuthorizeUrl(const GURL& issuer);
GURL TokenUrl(const GURL& issuer);
GURL ConfigurationUrl(const GURL& issuer);

struct SignInRequest {
  // Where OpenAI sends the browser back, on this Mac.
  std::string redirect_uri;
  // The ID uw was given for this account, or empty for a first sign-in.
  std::string client_id;
  // This Mac's own ID, kept from the first sign-in on.
  std::string host_id;
  std::string challenge;
  std::string state;
  std::string nonce;
  // From the account's last sign-in, when it had one.
  std::string id_token_hint;
  std::string login_hint;
};
GURL AuthorizationUrl(const GURL& issuer, const SignInRequest& request);

// A new ID for this Mac, in a form OpenAI takes.
std::string MakeHostId();

// A sign-in as uw keeps it in Keychain. Signing out drops the tokens and
// keeps the rest, so the next sign-in reuses the client ID.
struct Session {
  Session();
  Session(const Session&);
  Session& operator=(const Session&);
  ~Session();

  bool HasTokens() const { return !access.empty(); }

  std::string client_id;
  std::string email;
  std::string subject;
  std::string id_token;
  std::string access;
  std::string refresh;
  base::Time expires;
};
std::string SessionToJson(const Session& session);
std::optional<Session> SessionFromJson(std::string_view json);

enum class TokenAnswer {
  kOk,
  // The account signed in, but its grant lacks the plan scope.
  kNoPlan,
  kUnreadable,
};
// Fills `session` from the token endpoint's answer. A refresh replaces the
// refresh token too.
TokenAnswer ReadTokens(std::string_view json, base::Time now, Session& session);

// Whether a refresh failed for good, so the account must sign in again.
bool IsTerminalRefreshError(std::string_view json);

// What uw reads of the ID token. It comes straight from the token endpoint
// over TLS, which vouches for it in place of its signature (OpenID Connect
// Core 1.0, 3.1.3.7).
struct IdClaims {
  std::string issuer;
  std::vector<std::string> audience;
  std::string subject;
  std::string email;
  std::string nonce;
  base::Time expires;
};
std::optional<IdClaims> ReadIdToken(std::string_view id_token);

// The ID token's claims, when they match the sign-in uw asked for.
std::optional<IdClaims> CheckIdToken(std::string_view id_token,
                                     const GURL& issuer,
                                     std::string_view client_id,
                                     std::string_view nonce,
                                     base::Time now);

// A Responses request as the plan takes it: streamed, and without the
// fields it refuses.
std::string ForPlan(std::string_view responses_body);

// How a streamed answer ended: the completed response, in the shape of an
// answer that was not streamed, or the error that ended it.
struct StreamEnd {
  StreamEnd();
  StreamEnd(const StreamEnd&);
  StreamEnd& operator=(const StreamEnd&);
  ~StreamEnd();

  std::optional<std::string> response;
  std::string error_code;
  std::string error_message;
};
StreamEnd ReadStream(std::string_view events);

// The sentence a chat shows for a failed plan request.
std::string ErrorSentence(std::string_view code, std::string_view message);
// The HTTP status OpenAI gives an error of `code` when it answers at once,
// which uw shows for one that ends a stream too.
int ErrorStatus(std::string_view code);

// A model the plan offers for a picker.
struct PlanModel {
  std::string slug;
  std::string name;
};
std::optional<std::vector<PlanModel>> ReadModels(std::string_view json);

}  // namespace uw::chatgpt

#endif  // UW_COMPONENTS_AI_PROVIDERS_CHATGPT_PLAN_H_
