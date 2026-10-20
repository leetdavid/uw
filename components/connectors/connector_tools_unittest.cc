// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/connectors/connector_tools.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

mcp::Tool MakeTool(std::string name, std::string description) {
  mcp::Tool tool;
  tool.name = std::move(name);
  tool.description = std::move(description);
  return tool;
}

TEST(ConnectorToolsTest, NamesEveryToolAsAModelTakesIt) {
  EXPECT_EQ(ModelToolName("linear", "save_issue"), "linear__save_issue");
  EXPECT_EQ(ModelToolName("doop", "frames.get"), "doop__frames_get");
  EXPECT_EQ(ModelToolName(std::string(40, 'c'), std::string(40, 't')).size(),
            64u);
}

TEST(ConnectorToolsTest, AToolSchemaKeepsTheServersParameters) {
  mcp::Tool tool = MakeTool("save_issue", "Saves an issue.");
  tool.input_schema.Set("properties", base::DictValue());
  const base::DictValue schema = ModelToolSchema("linear", tool);
  EXPECT_EQ(*schema.FindStringByDottedPath("function.name"),
            "linear__save_issue");
  EXPECT_EQ(*schema.FindStringByDottedPath("function.parameters.type"),
            "object");
  EXPECT_TRUE(schema.FindDictByDottedPath("function.parameters.properties"));
  EXPECT_EQ(*LoadConnectorToolSchema().FindStringByDottedPath("function.name"),
            "connector");
}

TEST(ConnectorToolsTest, TheInstructionsListFirstSentences) {
  EXPECT_EQ(ConnectorsForInstructions({}), "");
  ConnectorTools linear;
  linear.name = "linear";
  linear.tools = {
      MakeTool("list_issues", "List issues. Filters by team or state."),
      MakeTool("save_issue", "Create or update an issue\nwith fields."),
      MakeTool("ping", "")};
  const std::string text = ConnectorsForInstructions({linear});
  EXPECT_NE(text.find("\n- linear\n  - list_issues: List issues.\n  - "
                      "save_issue: Create or update an issue\n  - ping"),
            std::string::npos)
      << text;
}

}  // namespace
}  // namespace uw
