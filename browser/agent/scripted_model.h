// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_SCRIPTED_MODEL_H_
#define UW_BROWSER_AGENT_SCRIPTED_MODEL_H_

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/values.h"

namespace net::test_server {
class EmbeddedTestServer;
struct HttpRequest;
class HttpResponse;
}  // namespace net::test_server

namespace uw {

// Plays an agent's model in browser tests, at /v1/chat/completions,
// /v1/messages, and /v1/responses of `server`. Each request gets the next of
// the scripted replies, in the shape the test scripts.
class ScriptedModel {
 public:
  // Before `server` starts.
  explicit ScriptedModel(net::test_server::EmbeddedTestServer& server);
  ScriptedModel(const ScriptedModel&) = delete;
  ScriptedModel& operator=(const ScriptedModel&) = delete;
  ~ScriptedModel();

  // The model's replies, in order. Once they run out, it repeats the last.
  void Script(std::vector<std::string> replies, int status = 200);
  // The body of every request so far.
  std::vector<std::string> Requests();
  // The models GET /v1/models lists: "test-agent" until a test names others.
  void ListModels(std::vector<std::string> names);

  // One tool call for Calls().
  static base::DictValue Call(std::string_view name,
                              base::DictValue args = base::DictValue());
  // A reply that makes the tool calls in `calls`.
  static std::string Calls(base::ListValue calls);
  // A reply that answers the user.
  static std::string Answer(std::string_view text);
  // `reply` with chat-completions usage of `input` and `output` tokens.
  static std::string WithUsage(std::string_view reply, int input, int output);
  // A reply that never comes, as when uw quits while the model thinks.
  static std::string Hang();

 private:
  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request);

  base::Lock lock_;
  std::deque<std::string> replies_ GUARDED_BY(lock_);
  int status_ GUARDED_BY(lock_) = 200;
  std::vector<std::string> requests_ GUARDED_BY(lock_);
  std::vector<std::string> models_ GUARDED_BY(lock_) = {"test-agent"};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_SCRIPTED_MODEL_H_
