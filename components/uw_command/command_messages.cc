// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/uw_command/command_messages.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"

namespace uw {

namespace {

std::string Line(const base::Value& id, std::string_view key, base::Value value) {
  base::DictValue reply;
  reply.Set("jsonrpc", "2.0");
  reply.Set("id", id.Clone());
  reply.Set(key, std::move(value));
  return base::WriteJson(reply).value_or("") + "\n";
}

}  // namespace

CommandRequest::CommandRequest() = default;
CommandRequest::CommandRequest(CommandRequest&&) = default;
CommandRequest& CommandRequest::operator=(CommandRequest&&) = default;
CommandRequest::~CommandRequest() = default;

std::optional<CommandRequest> ParseCommandRequest(std::string_view line) {
  std::optional<base::DictValue> message =
      base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
  if (!message) {
    return std::nullopt;
  }
  const base::Value* id = message->Find("id");
  const std::string* method = message->FindString("method");
  if (!id || !(id->is_int() || id->is_string()) || !method) {
    return std::nullopt;
  }
  CommandRequest request;
  request.id = id->Clone();
  request.method = *method;
  if (base::DictValue* params = message->FindDict("params")) {
    request.params = std::move(*params);
  }
  return request;
}

std::string CommandResult(const base::Value& id, base::Value result) {
  return Line(id, "result", std::move(result));
}

std::string CommandError(const base::Value& id,
                         int code,
                         std::string_view message) {
  base::DictValue error;
  error.Set("code", code);
  error.Set("message", message);
  return Line(id, "error", base::Value(std::move(error)));
}

std::string CommandNotification(std::string_view method, base::Value params) {
  base::DictValue notification;
  notification.Set("jsonrpc", "2.0");
  notification.Set("method", method);
  notification.Set("params", std::move(params));
  return base::WriteJson(notification).value_or("") + "\n";
}

}  // namespace uw
