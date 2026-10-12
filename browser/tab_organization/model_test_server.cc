// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/model_test_server.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"

namespace uw {

namespace {

const char* Otherwise(double new_task) {
  return new_task >= 0.5 ? "new_task" : "scratchpad";
}

}  // namespace

std::string DecisionsAnswer(const std::string& choice,
                            double confidence,
                            double new_task) {
  return R"({"answers":[{"type":"choice","name":"destination","choice":")" +
         choice + R"(","confidence":)" + base::NumberToString(confidence) +
         R"(},{"type":"choice","name":"otherwise","choice":")" +
         Otherwise(new_task) +
         R"(","probabilities":[{"value":"new_task","probability":)" +
         base::NumberToString(new_task) +
         R"(},{"value":"scratchpad","probability":)" +
         base::NumberToString(1.0 - new_task) + "}]}]}";
}

std::string SystemOneAnswer(const std::string& choice,
                            double confidence,
                            double new_task) {
  return R"({"answers":{"destination":{"type":"choice","choice":")" + choice +
         R"(","confidence":)" + base::NumberToString(confidence) +
         R"(,"probabilities":{}},"otherwise":{"type":"choice","choice":")" +
         Otherwise(new_task) + R"(","confidence":)" +
         base::NumberToString(new_task >= 0.5 ? 2 * new_task - 1
                                              : 1 - 2 * new_task) +
         R"(,"probabilities":{"new_task":)" + base::NumberToString(new_task) +
         R"(,"scratchpad":)" + base::NumberToString(1.0 - new_task) + "}}}}";
}

std::string ChatAnswer(const std::string& escaped_content) {
  return R"({"choices":[{"message":{"role":"assistant","content":")" +
         escaped_content + R"("}}]})";
}

ModelTestServer::ModelTestServer(net::test_server::EmbeddedTestServer* server) {
  server->RegisterRequestHandler(
      base::BindRepeating(&ModelTestServer::Serve, base::Unretained(this)));
}

ModelTestServer::~ModelTestServer() = default;

void ModelTestServer::Answer(const std::string& path, std::string body) {
  Fail(path, net::HTTP_OK, std::move(body));
}

void ModelTestServer::Fail(const std::string& path,
                           int status,
                           std::string body) {
  base::AutoLock hold(lock_);
  answers_[path] = {status, std::move(body), std::string()};
}

void ModelTestServer::Redirect(const std::string& path, std::string location) {
  base::AutoLock hold(lock_);
  answers_[path] = {net::HTTP_FOUND, std::string(), std::move(location)};
}

std::vector<ModelRequest> ModelTestServer::Requests() {
  base::AutoLock hold(lock_);
  return requests_;
}

std::unique_ptr<net::test_server::HttpResponse> ModelTestServer::Serve(
    const net::test_server::HttpRequest& request) {
  const std::string path(request.GetURL().path());
  if (!path.starts_with("/v1/")) {
    return nullptr;
  }
  base::AutoLock hold(lock_);
  const auto authorization = request.headers.find("Authorization");
  requests_.push_back({path, request.method_string, request.content,
                       authorization == request.headers.end()
                           ? std::string()
                           : authorization->second});
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  const auto answer = answers_.find(path);
  if (answer == answers_.end()) {
    response->set_code(net::HTTP_NOT_FOUND);
    return response;
  }
  const Reply& reply = answer->second;
  response->set_code(static_cast<net::HttpStatusCode>(reply.status));
  if (!reply.location.empty()) {
    response->AddCustomHeader("Location", reply.location);
    return response;
  }
  response->set_content_type("application/json");
  response->set_content(reply.body);
  return response;
}

}  // namespace uw
