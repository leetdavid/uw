// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_MODEL_TEST_SERVER_H_
#define UW_BROWSER_AI_PROVIDERS_MODEL_TEST_SERVER_H_

#include <map>
#include <memory>
#include <optional>
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
  // Anthropic takes its key in this header instead.
  std::string anthropic_key;
};

// A decision model's answer that the tab belongs to the task `choice` or, for
// "none", to no task. `new_task` is the probability it gives a tab outside
// every task of starting one of its own. `same_work` answers the name check:
// the probability that the tab is the same piece of work as the other tabs of
// its task. `hub` is the probability that the tab shows a hub, a page the
// user comes back to for many things.
std::string DecisionsAnswer(const std::string& choice,
                            double confidence,
                            double new_task = 0.0,
                            std::optional<double> same_work = std::nullopt,
                            double hub = 0.0);
std::string SystemOneAnswer(const std::string& choice,
                            double confidence,
                            double new_task = 0.0,
                            std::optional<double> same_work = std::nullopt,
                            double hub = 0.0);
// `escaped_content` is the assistant message, already escaped for JSON.
std::string ChatAnswer(const std::string& escaped_content);
// A decision model's answer to the command check, in the Decisions API's
// shape: how likely the step is safe and the danger it chose, and how likely
// it reads no sensitive file and the option it chose, such as "w2" for the
// step's second word.
std::string CheckDecisionsAnswer(double safe,
                                 const std::string& danger,
                                 double none,
                                 const std::string& file = "none");

// Plays every model provider under /v1/ on an embedded test server. Create it
// before the server starts and keep it alive while the server runs.
class ModelTestServer {
 public:
  explicit ModelTestServer(net::test_server::EmbeddedTestServer* server);
  ~ModelTestServer();

  // Sets the JSON body served at `path`, with its query when it has one, as
  // in /v1/models?output_modalities=decisions. Other /v1/ paths answer 404.
  void Answer(const std::string& path, std::string body);
  // Serves `body` at `path` to a request whose own body holds `text`, such
  // as the title of the tab it is about. Comes before Answer().
  void AnswerAbout(const std::string& path,
                   const std::string& text,
                   std::string body);
  // Serves `body` at `path` with an error status to a request whose own body
  // holds `text`. Comes before Answer().
  void FailAbout(const std::string& path,
                 const std::string& text,
                 int status,
                 std::string body);
  // Serves `body` at `path` with an error status.
  void Fail(const std::string& path, int status, std::string body);
  // Never answers `path`, like a model that takes its time.
  void Hold(const std::string& path);
  // Answers `path` with a redirect to `location`.
  void Redirect(const std::string& path, std::string location);
  std::vector<ModelRequest> Requests();

 private:
  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request);

  base::Lock lock_;
  std::vector<ModelRequest> requests_ GUARDED_BY(lock_);
  struct Reply {
    int status = 200;
    std::string body;
    std::string location;
    bool held = false;
  };

  std::map<std::string, Reply> answers_ GUARDED_BY(lock_);
  // Keyed by path and the text a request must hold.
  std::map<std::pair<std::string, std::string>, Reply> answers_about_
      GUARDED_BY(lock_);
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_MODEL_TEST_SERVER_H_
