// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_CONNECTORS_CONNECTOR_CONFIG_H_
#define UW_COMPONENTS_CONNECTORS_CONNECTOR_CONFIG_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/values.h"

namespace uw {

// A connector as the user added it: a program on this Mac or a web address
// that speaks MCP. See docs/plans/connectors.md.
struct ConnectorConfig {
  ConnectorConfig();
  ConnectorConfig(const ConnectorConfig&);
  ConnectorConfig& operator=(const ConnectorConfig&);
  ConnectorConfig(ConnectorConfig&&);
  ConnectorConfig& operator=(ConnectorConfig&&);
  ~ConnectorConfig();

  bool remote() const { return !url.empty(); }

  // The name the JSON gave it.
  std::string name;
  // A local connector's program and its arguments.
  std::string command;
  std::vector<std::string> args;
  // A remote connector's address.
  std::string url;
  // The secret parts, which go to Keychain: a local connector's
  // environment, and a remote one's headers.
  std::map<std::string, std::string> env;
  std::map<std::string, std::string> headers;
};

// The connectors in the JSON a server's README gives, such as
// {"mcpServers": {"name": {"command": ...}}}, or the inner object alone.
// The error says what is wrong, for the user.
base::expected<std::vector<ConnectorConfig>, std::string> ParseConnectors(
    std::string_view json);

// 1 to 64 letters, digits, hyphens, and underscores.
bool IsConnectorName(std::string_view name);

// A local connector that only runs mcp-remote, such as `npx -y mcp-remote
// <url> --header "Name: value"`, as the web connector it passes on to.
// `${VAR}` in a header takes the value from its "env". Nothing for any other
// program.
std::optional<ConnectorConfig> FromMcpRemote(const ConnectorConfig& local);

// The Streamable HTTP address to try for an address of the old HTTP+SSE
// transport: the same address ending in /mcp in place of /sse. Nothing for
// an address that doesn't end in /sse.
std::optional<std::string> StreamableAddress(std::string_view url);

// The connector as uw keeps it on disk: without its secrets, whose names
// stay so that Keychain can fill them in.
base::DictValue ConnectorToValue(const ConnectorConfig& connector);
// Nothing for a value that is not one. The secrets come back empty.
std::optional<ConnectorConfig> ConnectorFromValue(const base::DictValue& value);

// The secrets alone, as Keychain keeps them, and back.
std::string SecretsToJson(const ConnectorConfig& connector);
void SecretsFromJson(std::string_view json, ConnectorConfig& connector);

}  // namespace uw

#endif  // UW_COMPONENTS_CONNECTORS_CONNECTOR_CONFIG_H_
