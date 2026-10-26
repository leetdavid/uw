// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_CONNECTORS_CONNECTOR_TOOLS_H_
#define UW_COMPONENTS_CONNECTORS_CONNECTOR_TOOLS_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "uw/components/connectors/mcp.h"

// How the agent's model meets connectors: a short list in its instructions,
// the connector tool that loads one, and each loaded tool under a name of
// its own. See docs/plans/connectors.md#in-a-chat.
namespace uw {

// A connector's tools as the agent may use them: the ones the user left on.
struct ConnectorTools {
  ConnectorTools();
  ConnectorTools(const ConnectorTools&);
  ConnectorTools& operator=(const ConnectorTools&);
  ~ConnectorTools();

  std::string name;
  std::vector<mcp::Tool> tools;
};

// "linear__save_issue": the connector's name and the tool's, with any
// character a model refuses as "_", cut to 64 characters.
std::string ModelToolName(std::string_view connector, std::string_view tool);

// A loaded tool, in chat-completions form.
base::DictValue ModelToolSchema(std::string_view connector,
                                const mcp::Tool& tool);

// The tool that loads one connector's tools for the rest of the chat.
inline constexpr char kLoadConnectorTool[] = "connector";
base::DictValue LoadConnectorToolSchema();

// What the instructions say about `connectors`: how to use them, and each
// one with its tools' names and the first sentence of each description.
// Empty for none.
std::string ConnectorsForInstructions(
    const std::vector<ConnectorTools>& connectors);

}  // namespace uw

#endif  // UW_COMPONENTS_CONNECTORS_CONNECTOR_TOOLS_H_
