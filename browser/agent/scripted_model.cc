// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/scripted_model.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace uw {
namespace {

constexpr char kHang[] = "(hang)";

std::string Choice(base::DictValue message) {
  base::ListValue choices;
  choices.Append(base::DictValue().Set("message", std::move(message)));
  return *base::WriteJson(base::DictValue().Set("choices", std::move(choices)));
}

}  // namespace

ScriptedModel::ScriptedModel(net::test_server::EmbeddedTestServer& server) {
  server.RegisterRequestHandler(
      base::BindRepeating(&ScriptedModel::Serve, base::Unretained(this)));
}

ScriptedModel::~ScriptedModel() = default;

void ScriptedModel::Script(std::vector<std::string> replies, int status) {
  base::AutoLock lock(lock_);
  replies_.assign(replies.begin(), replies.end());
  status_ = status;
}

std::vector<std::string> ScriptedModel::Requests() {
  base::AutoLock lock(lock_);
  return requests_;
}

// static
base::DictValue ScriptedModel::Call(std::string_view name,
                                    base::DictValue args) {
  return base::DictValue().Set("name", name).Set("args", std::move(args));
}

// static
std::string ScriptedModel::Calls(base::ListValue calls) {
  base::ListValue list;
  for (const base::Value& call : calls) {
    list.Append(
        base::DictValue()
            .Set("id", "call_" + base::NumberToString(list.size()))
            .Set("type", "function")
            .Set("function",
                 base::DictValue()
                     .Set("name", *call.GetDict().FindString("name"))
                     .Set("arguments",
                          *base::WriteJson(*call.GetDict().FindDict("args")))));
  }
  return Choice(base::DictValue()
                    .Set("role", "assistant")
                    .Set("tool_calls", std::move(list)));
}

// static
std::string ScriptedModel::Answer(std::string_view text) {
  return Choice(
      base::DictValue().Set("role", "assistant").Set("content", text));
}

// static
std::string ScriptedModel::Hang() {
  return kHang;
}

std::unique_ptr<net::test_server::HttpResponse> ScriptedModel::Serve(
    const net::test_server::HttpRequest& request) {
  // Chat completions, Anthropic Messages, and OpenAI Responses.
  if (request.relative_url != "/v1/chat/completions" &&
      request.relative_url != "/v1/messages" &&
      request.relative_url != "/v1/responses") {
    return nullptr;
  }
  base::AutoLock lock(lock_);
  requests_.push_back(request.content);
  if (!replies_.empty() && replies_.front() == kHang) {
    return std::make_unique<net::test_server::HungResponse>();
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(static_cast<net::HttpStatusCode>(status_));
  response->set_content_type("application/json");
  response->set_content(replies_.empty() ? std::string() : replies_.front());
  if (replies_.size() > 1) {
    replies_.pop_front();
  }
  return response;
}

}  // namespace uw
