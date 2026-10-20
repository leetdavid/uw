// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_CONNECTORS_MCP_H_
#define UW_COMPONENTS_CONNECTORS_MCP_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// The JSON-RPC messages uw sends a connector and reads back, in the two
// generations of MCP it speaks. See docs/plans/connectors.md.
namespace uw::mcp {

inline constexpr char kModernVersion[] = "2026-07-28";
inline constexpr char kLegacyVersion[] = "2025-11-25";

// A modern server takes the version with every request. A legacy one starts
// with the initialize handshake.
enum class Era { kModern, kLegacy };

// The body of a request. A modern one carries uw's version, name, and
// capabilities in its params' _meta.
std::string RequestBody(int id,
                        Era era,
                        std::string_view method,
                        base::DictValue params = {});
// The answer to a legacy server's request, which uw never asks for: an
// empty result for ping, and "method not found" for the rest.
std::string AnswerToServer(const base::Value& id, std::string_view method);
// The legacy handshake's params, and the notification that ends it.
base::DictValue InitializeParams();
std::string InitializedNotification();

struct Error {
  Error();
  Error(const Error&);
  Error& operator=(const Error&);
  ~Error();

  int code = 0;
  std::string message;
  // The versions an unsupported-version error lists.
  std::vector<std::string> supported;
};

struct Message {
  Message();
  Message(Message&&);
  Message& operator=(Message&&);
  ~Message();

  // A response's id. uw only sends whole-number ids.
  std::optional<int> id;
  // A notification's or a legacy server's request's method.
  std::string method;
  // A legacy server's request carries its own id, to answer with.
  base::Value request_id;
  base::DictValue result;
  std::optional<Error> error;
};

// One message, or nothing for text that is not one.
std::optional<Message> ParseMessage(std::string_view json);
// The messages in a text/event-stream body, in order.
std::vector<Message> ParseEventStream(std::string_view body);

// Whether only a modern server sends `error`: a header mismatch, a missing
// capability, or an unsupported version.
bool IsModernError(const Error& error);

struct Tool {
  Tool();
  Tool(const Tool&);
  Tool& operator=(const Tool&);
  Tool(Tool&&);
  Tool& operator=(Tool&&);
  ~Tool();

  std::string name;
  std::string description;
  base::DictValue input_schema;
  // The server says the tool only reads. A hint, not a promise.
  bool read_only = false;
};

struct Prompt {
  struct Argument {
    std::string name;
    std::string description;
    bool required = false;
  };

  Prompt();
  Prompt(const Prompt&);
  Prompt& operator=(const Prompt&);
  ~Prompt();

  std::string name;
  std::string description;
  std::vector<Argument> arguments;
};

std::vector<Tool> ParseTools(const base::DictValue& result);
std::vector<Prompt> ParsePrompts(const base::DictValue& result);
// The cursor for a list's next page, or nothing on its last page.
std::optional<std::string> NextCursor(const base::DictValue& result);

base::DictValue ToolToValue(const Tool& tool);
std::optional<Tool> ToolFromValue(const base::DictValue& value);
base::DictValue PromptToValue(const Prompt& prompt);
std::optional<Prompt> PromptFromValue(const base::DictValue& value);

// What a tool call or a prompt gave, for the model: text, and images as
// data: URLs.
struct Content {
  Content();
  Content(Content&&);
  Content& operator=(Content&&);
  ~Content();

  std::string text;
  std::vector<std::string> images;
  // The tool says it failed.
  bool failed = false;
};

Content ParseToolResult(const base::DictValue& result);
Content ParsePromptResult(const base::DictValue& result);

}  // namespace uw::mcp

#endif  // UW_COMPONENTS_CONNECTORS_MCP_H_
