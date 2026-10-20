// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_FAKE_MCP_SERVER_H_
#define UW_BROWSER_CONNECTORS_FAKE_MCP_SERVER_H_

#include <optional>
#include <string>
#include <string_view>

namespace uw {

// A connector for tests. It speaks modern MCP, or legacy MCP that starts
// with initialize. Its tools:
// - echo, which only reads, answers with its text argument.
// - save_note answers "Saved" and the title.
// - picture answers with a one-pixel PNG.
// - env answers with the environment variable its name argument names.
// - crash asks the program around it to stop.
// Its tools come in two pages. Its prompt greet needs a name.
class FakeMcpServer {
 public:
  explicit FakeMcpServer(bool legacy);

  // The answer to one message, or nothing for a notification.
  std::optional<std::string> Answer(std::string_view message);

  bool crash() const { return crash_; }
  // The session a legacy server gave out at initialize.
  const std::string& session() const { return session_; }

 private:
  const bool legacy_;
  bool initialized_ = false;
  bool crash_ = false;
  std::string session_;
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_FAKE_MCP_SERVER_H_
