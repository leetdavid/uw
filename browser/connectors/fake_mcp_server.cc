// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/connectors/fake_mcp_server.h"

#include <stdlib.h>

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/values.h"

namespace uw {
namespace {

// A one-pixel PNG.
constexpr char kPixel[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGA"
    "hKmMIQAAAABJRU5ErkJggg==";

std::string Reply(const base::Value& id, base::DictValue result) {
  return base::WriteJson(base::DictValue()
                             .Set("jsonrpc", "2.0")
                             .Set("id", id.Clone())
                             .Set("result", std::move(result)))
      .value_or("");
}

std::string Refuse(const base::Value& id,
                   int code,
                   std::string_view text,
                   base::DictValue data = {}) {
  return base::WriteJson(base::DictValue()
                             .Set("jsonrpc", "2.0")
                             .Set("id", id.Clone())
                             .Set("error", base::DictValue()
                                               .Set("code", code)
                                               .Set("message", text)
                                               .Set("data", std::move(data))))
      .value_or("");
}

base::DictValue Text(std::string_view text) {
  base::ListValue content;
  content.Append(base::DictValue().Set("type", "text").Set("text", text));
  return base::DictValue().Set("content", std::move(content));
}

base::DictValue Tool(std::string_view name,
                     std::string_view description,
                     std::string_view argument,
                     bool read_only) {
  base::DictValue properties;
  base::ListValue required;
  if (!argument.empty()) {
    properties.Set(argument, base::DictValue().Set("type", "string"));
    required.Append(argument);
  }
  return base::DictValue()
      .Set("name", name)
      .Set("description", description)
      .Set("inputSchema", base::DictValue()
                              .Set("type", "object")
                              .Set("properties", std::move(properties))
                              .Set("required", std::move(required)))
      .Set("annotations", base::DictValue().Set("readOnlyHint", read_only));
}

}  // namespace

FakeMcpServer::FakeMcpServer(bool legacy) : legacy_(legacy) {}

std::optional<std::string> FakeMcpServer::Answer(std::string_view text) {
  std::optional<base::DictValue> message =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!message) {
    return Refuse(base::Value(), -32700, "Parse error");
  }
  const base::Value* id = message->Find("id");
  const std::string* method = message->FindString("method");
  if (!id || !method) {
    return std::nullopt;
  }
  const base::DictValue* params = message->FindDict("params");
  const base::DictValue none;
  if (!params) {
    params = &none;
  }

  if (legacy_) {
    if (*method == "initialize") {
      initialized_ = true;
      session_ = "session-1";
      return Reply(
          *id, base::DictValue()
                   .Set("protocolVersion", "2025-11-25")
                   .Set("capabilities", base::DictValue()
                                            .Set("tools", base::DictValue())
                                            .Set("prompts", base::DictValue()))
                   .Set("serverInfo", base::DictValue().Set("name", "fake")));
    }
    if (!initialized_) {
      return Refuse(*id, -32601, "Method not found");
    }
  } else {
    const base::DictValue* meta = params->FindDict("_meta");
    const std::string* version =
        meta ? meta->FindString("io.modelcontextprotocol/protocolVersion")
             : nullptr;
    if (!version) {
      return Refuse(*id, -32602, "Every request needs _meta");
    }
    if (*version != "2026-07-28") {
      base::ListValue supported;
      supported.Append("2026-07-28");
      return Refuse(*id, -32022, "Unsupported protocol version",
                    base::DictValue().Set("supported", std::move(supported)));
    }
    if (*method == "server/discover") {
      base::ListValue versions;
      versions.Append("2026-07-28");
      return Reply(*id, base::DictValue()
                            .Set("resultType", "complete")
                            .Set("supportedVersions", std::move(versions))
                            .Set("capabilities",
                                 base::DictValue()
                                     .Set("tools", base::DictValue())
                                     .Set("prompts", base::DictValue())));
    }
  }

  if (*method == "tools/list") {
    const std::string* cursor = params->FindString("cursor");
    base::ListValue tools;
    base::DictValue result;
    if (!cursor) {
      tools.Append(Tool("echo", "Says the text back. Only reads.", "text",
                        /*read_only=*/true));
      tools.Append(Tool("save_note", "Saves a note.", "title", false));
      tools.Append(Tool("picture", "Shows a picture.", "", true));
      result.Set("nextCursor", "2");
    } else {
      tools.Append(Tool("env", "Reads an environment variable.", "name", true));
      tools.Append(Tool("crash", "Stops the server.", "", false));
    }
    result.Set("tools", std::move(tools));
    return Reply(*id, std::move(result));
  }
  if (*method == "tools/call") {
    const std::string* name = params->FindString("name");
    const base::DictValue* arguments = params->FindDict("arguments");
    const auto argument = [&](std::string_view key) {
      const std::string* value =
          arguments ? arguments->FindString(key) : nullptr;
      return value ? *value : std::string();
    };
    if (!name) {
      return Refuse(*id, -32602, "No tool");
    }
    if (*name == "echo") {
      return Reply(*id, Text(argument("text")));
    }
    if (*name == "save_note") {
      return Reply(*id, Text(base::StrCat({"Saved ", argument("title")})));
    }
    if (*name == "picture") {
      base::ListValue content;
      content.Append(base::DictValue()
                         .Set("type", "image")
                         .Set("data", kPixel)
                         .Set("mimeType", "image/png"));
      return Reply(*id, base::DictValue().Set("content", std::move(content)));
    }
    if (*name == "env") {
      const char* value = getenv(argument("name").c_str());
      return Reply(*id, Text(value ? value : "(unset)"));
    }
    if (*name == "crash") {
      crash_ = true;
      return std::nullopt;
    }
    return Reply(*id, Text("No such tool").Set("isError", true));
  }
  if (*method == "prompts/list") {
    base::ListValue arguments;
    arguments.Append(base::DictValue()
                         .Set("name", "name")
                         .Set("description", "Who to greet")
                         .Set("required", true));
    base::ListValue prompts;
    prompts.Append(base::DictValue()
                       .Set("name", "greet")
                       .Set("description", "Greets someone.")
                       .Set("arguments", std::move(arguments)));
    return Reply(*id, base::DictValue().Set("prompts", std::move(prompts)));
  }
  if (*method == "prompts/get") {
    const std::string* person =
        params->FindStringByDottedPath("arguments.name");
    base::ListValue messages;
    messages.Append(
        base::DictValue()
            .Set("role", "user")
            .Set("content",
                 base::DictValue()
                     .Set("type", "text")
                     .Set("text", base::StrCat({"Say hello to ",
                                                person ? *person : "?"}))));
    return Reply(*id, base::DictValue().Set("messages", std::move(messages)));
  }
  return Refuse(*id, -32601, "Method not found");
}

}  // namespace uw
