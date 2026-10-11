// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_MODEL_TEST_SERVER_H_
#define UW_BROWSER_TAB_ORGANIZATION_MODEL_TEST_SERVER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"

namespace net::test_server {
class EmbeddedTestServer;
struct HttpRequest;
class HttpResponse;
}  // namespace net::test_server

namespace uw {

struct ModelRequest {
  std::string path;
  std::string method;
  std::string body;
  std::string authorization;
};

std::string DecisionsAnswer(const std::string& choice, double confidence);
std::string SystemOneAnswer(const std::string& choice, double confidence);
// `escaped_content` is the assistant message, already escaped for JSON.
std::string ChatAnswer(const std::string& escaped_content);

// Plays every model provider under /v1/ on an embedded test server. Create it
// before the server starts and keep it alive while the server runs.
class ModelTestServer {
 public:
  explicit ModelTestServer(net::test_server::EmbeddedTestServer* server);
  ~ModelTestServer();

  // Sets the JSON body served at `path`. Other /v1/ paths answer 404.
  void Answer(const std::string& path, std::string body);
  std::vector<ModelRequest> Requests();

 private:
  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request);

  base::Lock lock_;
  std::vector<ModelRequest> requests_ GUARDED_BY(lock_);
  std::map<std::string, std::string> answers_ GUARDED_BY(lock_);
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_MODEL_TEST_SERVER_H_
