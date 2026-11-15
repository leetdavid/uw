// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/chatgpt_plan.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"

namespace uw::chatgpt {
namespace {

constexpr char kAppName[] = "uw";
constexpr char kScopes[] =
    "openid profile email offline_access resource.invoke "
    "chatgpt.tokens.use.direct";

// Request fields the plan refuses. See
// https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations.
constexpr const char* kRefusedFields[] = {"background",
                                          "conversation",
                                          "max_output_tokens",
                                          "max_tool_calls",
                                          "metadata",
                                          "moderation",
                                          "multi_agent",
                                          "previous_response_id",
                                          "prompt",
                                          "prompt_cache_retention",
                                          "safety_identifier",
                                          "temperature",
                                          "top_logprobs",
                                          "top_p",
                                          "truncation",
                                          "user"};

// Refresh errors after which only a new sign-in helps.
constexpr const char* kTerminalRefreshErrors[] = {"invalid_grant",
                                                  "invalid_refresh_token",
                                                  "token_expired",
                                                  "refresh_token_expired",
                                                  "refresh_token_invalidated",
                                                  "refresh_token_reused"};

GURL Under(const GURL& issuer, std::string_view path) {
  std::string base = issuer.spec();
  while (base.ends_with('/')) {
    base.pop_back();
  }
  return GURL(base::StrCat({base, path}));
}

std::string Trimmed(const GURL& url) {
  std::string spec = url.spec();
  while (spec.ends_with('/')) {
    spec.pop_back();
  }
  return spec;
}

std::string StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

// An error's code and message, from {"error": "..."} or {"error": {...}}.
std::pair<std::string, std::string> ErrorOf(const base::DictValue& dict) {
  if (const std::string* code = dict.FindString("error")) {
    return {*code, StringOr(dict, "error_description")};
  }
  const base::DictValue* error = dict.FindDict("error");
  if (!error) {
    return {StringOr(dict, "code"), StringOr(dict, "message")};
  }
  return {StringOr(*error, "code"), StringOr(*error, "message")};
}

}  // namespace

GURL AuthorizeUrl(const GURL& issuer) {
  return Under(issuer, "/api/accounts/authorize");
}

GURL TokenUrl(const GURL& issuer) {
  return Under(issuer, "/api/accounts/oauth/token");
}

GURL ConfigurationUrl(const GURL& issuer) {
  return Under(issuer, "/.well-known/openid-configuration");
}

GURL AuthorizationUrl(const GURL& issuer, const SignInRequest& request) {
  const bool first = request.client_id.empty();
  std::vector<std::pair<std::string_view, std::string_view>> fields = {
      {"client_id", first ? std::string_view(kFirstClientId)
                          : std::string_view(request.client_id)}};
  if (first) {
    fields.emplace_back("agent_name_hint", kAppName);
  }
  fields.insert(fields.end(), {{"ext_agent_host_id", request.host_id},
                               {"id_token_hint", request.id_token_hint},
                               {"login_hint", request.login_hint},
                               {"response_type", "code"},
                               {"redirect_uri", request.redirect_uri},
                               {"scope", kScopes},
                               {"resource", kApi},
                               {"state", request.state},
                               {"nonce", request.nonce},
                               {"code_challenge_method", "S256"},
                               {"code_challenge", request.challenge}});
  std::string query;
  for (const auto& [key, value] : fields) {
    if (value.empty()) {
      continue;
    }
    base::StrAppend(&query, {query.empty() ? "" : "&", key, "=",
                             base::EscapeQueryParamValue(value, true)});
  }
  GURL::Replacements replacements;
  replacements.SetQueryStr(query);
  return AuthorizeUrl(issuer).ReplaceComponents(replacements);
}

std::string MakeHostId() {
  return "urn:uuid:" + base::Uuid::GenerateRandomV4().AsLowercaseString();
}

Session::Session() = default;
Session::Session(const Session&) = default;
Session& Session::operator=(const Session&) = default;
Session::~Session() = default;

std::string SessionToJson(const Session& session) {
  return base::WriteJson(
             base::DictValue()
                 .Set("client_id", session.client_id)
                 .Set("email", session.email)
                 .Set("subject", session.subject)
                 .Set("id_token", session.id_token)
                 .Set("access", session.access)
                 .Set("refresh", session.refresh)
                 .Set("expires", session.expires.InSecondsFSinceUnixEpoch()))
      .value_or("");
}

std::optional<Session> SessionFromJson(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict || StringOr(*dict, "client_id").empty()) {
    return std::nullopt;
  }
  Session session;
  session.client_id = StringOr(*dict, "client_id");
  session.email = StringOr(*dict, "email");
  session.subject = StringOr(*dict, "subject");
  session.id_token = StringOr(*dict, "id_token");
  session.access = StringOr(*dict, "access");
  session.refresh = StringOr(*dict, "refresh");
  session.expires = base::Time::FromSecondsSinceUnixEpoch(
      dict->FindDouble("expires").value_or(0));
  return session;
}

TokenAnswer ReadTokens(std::string_view json,
                       base::Time now,
                       Session& session) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const std::string access = dict ? StringOr(*dict, "access_token") : "";
  if (access.empty()) {
    return TokenAnswer::kUnreadable;
  }
  if (const std::string* scope = dict->FindString("scope")) {
    const std::vector<std::string_view> scopes = base::SplitStringPiece(
        *scope, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (!std::ranges::contains(scopes, std::string_view(kPlanScope))) {
      return TokenAnswer::kNoPlan;
    }
  }
  session.access = access;
  if (const std::string* refresh = dict->FindString("refresh_token")) {
    session.refresh = *refresh;
  }
  if (const std::string* id_token = dict->FindString("id_token")) {
    session.id_token = *id_token;
  }
  session.expires =
      now + base::Seconds(dict->FindDouble("expires_in").value_or(3600));
  return TokenAnswer::kOk;
}

bool IsTerminalRefreshError(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return false;
  }
  const std::string code = ErrorOf(*dict).first;
  return std::ranges::contains(kTerminalRefreshErrors, code);
}

std::optional<IdClaims> ReadIdToken(std::string_view id_token) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      id_token, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  std::string payload;
  if (parts.size() != 3 ||
      !base::Base64UrlDecode(
          parts[1], base::Base64UrlDecodePolicy::IGNORE_PADDING, &payload)) {
    return std::nullopt;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  IdClaims claims;
  claims.issuer = StringOr(*dict, "iss");
  claims.subject = StringOr(*dict, "sub");
  claims.email = StringOr(*dict, "email");
  claims.nonce = StringOr(*dict, "nonce");
  claims.expires = base::Time::FromSecondsSinceUnixEpoch(
      dict->FindDouble("exp").value_or(0));
  if (const std::string* one = dict->FindString("aud")) {
    claims.audience.push_back(*one);
  } else if (const base::ListValue* audiences = dict->FindList("aud")) {
    for (const base::Value& audience : *audiences) {
      if (audience.is_string()) {
        claims.audience.push_back(audience.GetString());
      }
    }
  }
  return claims;
}

std::optional<IdClaims> CheckIdToken(std::string_view id_token,
                                     const GURL& issuer,
                                     std::string_view client_id,
                                     std::string_view nonce,
                                     base::Time now) {
  std::optional<IdClaims> claims = ReadIdToken(id_token);
  if (!claims || claims->issuer != Trimmed(issuer) ||
      !std::ranges::contains(claims->audience, client_id) ||
      claims->nonce != nonce || claims->expires <= now ||
      claims->subject.empty()) {
    return std::nullopt;
  }
  return claims;
}

std::string ForPlan(std::string_view responses_body) {
  std::optional<base::DictValue> request =
      base::JSONReader::ReadDict(responses_body, base::JSON_PARSE_RFC);
  if (!request) {
    return std::string(responses_body);
  }
  for (const char* field : kRefusedFields) {
    request->Remove(field);
  }
  request->Set("store", false);
  request->Set("stream", true);
  return base::WriteJson(*request).value_or(std::string(responses_body));
}

StreamEnd::StreamEnd() = default;
StreamEnd::StreamEnd(const StreamEnd&) = default;
StreamEnd& StreamEnd::operator=(const StreamEnd&) = default;
StreamEnd::~StreamEnd() = default;

StreamEnd ReadStream(std::string_view events) {
  StreamEnd end;
  std::string data;
  const auto read_event = [&] {
    std::optional<base::DictValue> event =
        base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    data.clear();
    if (!event) {
      return false;
    }
    const std::string type = StringOr(*event, "type");
    const base::DictValue* response = event->FindDict("response");
    if (type == "response.completed" && response) {
      end.response = base::WriteJson(*response);
      return true;
    }
    if (type == "response.failed" || type == "response.incomplete") {
      const base::DictValue* error =
          response ? response->FindDict("error") : nullptr;
      const base::DictValue* incomplete =
          response ? response->FindDict("incomplete_details") : nullptr;
      end.error_code = error        ? StringOr(*error, "code")
                       : incomplete ? StringOr(*incomplete, "reason")
                                    : type;
      end.error_message = error ? StringOr(*error, "message") : "";
      return true;
    }
    if (type == "error") {
      std::tie(end.error_code, end.error_message) = ErrorOf(*event);
      return true;
    }
    return false;
  };
  for (std::string_view line : base::SplitStringPiece(
           events, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    line = base::TrimString(line, "\r", base::TRIM_TRAILING);
    if (line.empty()) {
      if (!data.empty() && read_event()) {
        return end;
      }
      continue;
    }
    if (line.starts_with("data:")) {
      line.remove_prefix(5);
      base::StrAppend(&data, {data.empty() ? "" : "\n",
                              base::TrimString(line, " ", base::TRIM_LEADING)});
    }
  }
  if (!data.empty() && read_event()) {
    return end;
  }
  if (end.error_code.empty()) {
    end.error_code = "stream_ended";
  }
  return end;
}

std::string ErrorSentence(std::string_view code, std::string_view message) {
  if (code == "subscription_sharing_usage_limit_exceeded") {
    return base::StrCat(
        {message.empty() ? "You've used your ChatGPT plan's allowance for uw."
                         : message,
         " See ", kUsagePage, "."});
  }
  if (!message.empty()) {
    return std::string(message);
  }
  if (code == "stream_ended") {
    return "OpenAI's answer stopped before it was complete.";
  }
  return base::StrCat({"OpenAI ended the answer: ", code, "."});
}

int ErrorStatus(std::string_view code) {
  if (code == "subscription_sharing_usage_limit_exceeded") {
    return 429;
  }
  if (code.ends_with("_unavailable")) {
    return 503;
  }
  return 400;
}

std::optional<std::vector<PlanModel>> ReadModels(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* models = dict ? dict->FindList("models") : nullptr;
  if (!models) {
    return std::nullopt;
  }
  std::vector<PlanModel> listed;
  for (const base::Value& entry : *models) {
    const base::DictValue* model = entry.GetIfDict();
    if (!model || StringOr(*model, "visibility") != "list" ||
        StringOr(*model, "slug").empty()) {
      continue;
    }
    const std::string slug = StringOr(*model, "slug");
    const std::string name = StringOr(*model, "display_name");
    listed.push_back({slug, name.empty() ? slug : name});
  }
  return listed;
}

}  // namespace uw::chatgpt
