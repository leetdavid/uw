// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/connectors/connector_config.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

base::unexpected<std::string> Fail(std::string_view name,
                                   std::string_view problem) {
  return base::unexpected(base::StrCat({name, ": ", problem}));
}

// A JSON object of strings, such as "env" or "headers". False when a value
// is not a string.
bool ReadStrings(const base::DictValue* dict,
                 std::map<std::string, std::string>& out) {
  if (!dict) {
    return true;
  }
  for (const auto [key, value] : *dict) {
    if (!value.is_string()) {
      return false;
    }
    out[key] = value.GetString();
  }
  return true;
}

base::ListValue Keys(const std::map<std::string, std::string>& values) {
  base::ListValue keys;
  for (const auto& [key, value] : values) {
    keys.Append(key);
  }
  return keys;
}

base::DictValue Strings(const std::map<std::string, std::string>& values) {
  base::DictValue dict;
  for (const auto& [key, value] : values) {
    dict.Set(key, value);
  }
  return dict;
}

base::expected<ConnectorConfig, std::string> ReadConnector(
    const std::string& name,
    const base::Value& value) {
  if (!IsConnectorName(name)) {
    return Fail(name.empty() ? "A connector" : name,
                "name it with letters, digits, hyphens, and underscores.");
  }
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return Fail(name, "this isn't a connector.");
  }
  ConnectorConfig connector;
  connector.name = name;
  const std::string* type = dict->FindString("type");
  const std::string* url = dict->FindString("url");
  const std::string* command = dict->FindString("command");
  if (type && *type == "sse") {
    return Fail(name,
                "uw doesn't speak the old HTTP+SSE transport. Use the "
                "server's Streamable HTTP address.");
  }
  const bool remote = type ? (*type == "http" || *type == "streamable-http")
                           : (url && !command);
  if (type && !remote && *type != "stdio") {
    return Fail(name,
                base::StrCat({"uw doesn't know the type \"", *type, "\"."}));
  }
  if (remote) {
    if (!url || url->empty()) {
      return Fail(name, "a web connector needs a \"url\".");
    }
    if (!IsValidProviderEndpoint(*url)) {
      return Fail(name,
                  "the address must be https, or http on this Mac or a "
                  "private network.");
    }
    connector.url = *url;
    if (!ReadStrings(dict->FindDict("headers"), connector.headers)) {
      return Fail(name, "each header must be a string.");
    }
    return connector;
  }
  if (!command || base::TrimWhitespaceASCII(*command, base::TRIM_ALL).empty()) {
    return Fail(name, "a connector on this Mac needs a \"command\".");
  }
  connector.command = *command;
  if (const base::Value* args = dict->Find("args")) {
    if (!args->is_list()) {
      return Fail(name, "\"args\" must be a list of strings.");
    }
    for (const base::Value& arg : args->GetList()) {
      if (!arg.is_string()) {
        return Fail(name, "\"args\" must be a list of strings.");
      }
      connector.args.push_back(arg.GetString());
    }
  }
  if (!ReadStrings(dict->FindDict("env"), connector.env)) {
    return Fail(name, "each value in \"env\" must be a string.");
  }
  return connector;
}

}  // namespace

ConnectorConfig::ConnectorConfig() = default;
ConnectorConfig::ConnectorConfig(const ConnectorConfig&) = default;
ConnectorConfig& ConnectorConfig::operator=(const ConnectorConfig&) = default;
ConnectorConfig::ConnectorConfig(ConnectorConfig&&) = default;
ConnectorConfig& ConnectorConfig::operator=(ConnectorConfig&&) = default;
ConnectorConfig::~ConnectorConfig() = default;

base::expected<std::vector<ConnectorConfig>, std::string> ParseConnectors(
    std::string_view json) {
  base::JSONReader::Result parsed =
      base::JSONReader::ReadAndReturnValueWithError(
          json, base::JSON_PARSE_CHROMIUM_EXTENSIONS |
                    base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed.has_value()) {
    return base::unexpected(
        base::StrCat({"That isn't JSON: the problem is at line ",
                      base::NumberToString(parsed.error().line), "."}));
  }
  const base::DictValue* top = parsed->GetIfDict();
  if (!top) {
    return base::unexpected(
        "Paste a JSON object, such as {\"mcpServers\": {...}}.");
  }
  const base::DictValue* servers = top->FindDict("mcpServers");
  if (!servers) {
    servers = top->FindDict("servers");
  }
  if (!servers) {
    if (top->Find("command") || top->Find("url")) {
      return base::unexpected(
          "Give the connector a name: {\"name\": {\"command\": ...}}.");
    }
    servers = top;
  }
  if (servers->empty()) {
    return base::unexpected("There's no connector in it.");
  }
  std::vector<ConnectorConfig> connectors;
  for (const auto [name, value] : *servers) {
    base::expected<ConnectorConfig, std::string> connector =
        ReadConnector(name, value);
    if (!connector.has_value()) {
      return base::unexpected(std::move(connector).error());
    }
    connectors.push_back(std::move(connector).value());
  }
  return connectors;
}

bool IsConnectorName(std::string_view name) {
  return !name.empty() && name.size() <= 64 &&
         std::ranges::all_of(name, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_';
         });
}

base::DictValue ConnectorToValue(const ConnectorConfig& connector) {
  base::DictValue value;
  value.Set("name", connector.name);
  if (connector.remote()) {
    value.Set("url", connector.url);
    value.Set("header_names", Keys(connector.headers));
    return value;
  }
  base::ListValue args;
  for (const std::string& arg : connector.args) {
    args.Append(arg);
  }
  value.Set("command", connector.command);
  value.Set("args", std::move(args));
  value.Set("env_names", Keys(connector.env));
  return value;
}

std::optional<ConnectorConfig> ConnectorFromValue(
    const base::DictValue& value) {
  const std::string* name = value.FindString("name");
  if (!name || !IsConnectorName(*name)) {
    return std::nullopt;
  }
  ConnectorConfig connector;
  connector.name = *name;
  const auto names = [](const base::ListValue* list,
                        std::map<std::string, std::string>& out) {
    if (list) {
      for (const base::Value& key : *list) {
        if (key.is_string()) {
          out[key.GetString()];
        }
      }
    }
  };
  if (const std::string* url = value.FindString("url")) {
    connector.url = *url;
    names(value.FindList("header_names"), connector.headers);
    return connector;
  }
  const std::string* command = value.FindString("command");
  if (!command || command->empty()) {
    return std::nullopt;
  }
  connector.command = *command;
  if (const base::ListValue* args = value.FindList("args")) {
    for (const base::Value& arg : *args) {
      if (arg.is_string()) {
        connector.args.push_back(arg.GetString());
      }
    }
  }
  names(value.FindList("env_names"), connector.env);
  return connector;
}

std::string SecretsToJson(const ConnectorConfig& connector) {
  return base::WriteJson(base::DictValue()
                             .Set("env", Strings(connector.env))
                             .Set("headers", Strings(connector.headers)))
      .value_or("");
}

void SecretsFromJson(std::string_view json, ConnectorConfig& connector) {
  std::optional<base::DictValue> secrets =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!secrets) {
    return;
  }
  ReadStrings(secrets->FindDict("env"), connector.env);
  ReadStrings(secrets->FindDict("headers"), connector.headers);
}

}  // namespace uw
