// Copyright 2026 The uw Authors. All rights reserved.

// The test connector as a program: one JSON-RPC message per line on its
// input and output. --legacy makes it a legacy server. --fail=TEXT writes
// TEXT to its error output and quits at once.

#include <stdio.h>

#include <iostream>
#include <string>

#include "base/command_line.h"
#include "uw/browser/connectors/fake_mcp_server.h"

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch("fail")) {
    fprintf(stderr, "starting\n%s\n",
            command_line.GetSwitchValueASCII("fail").c_str());
    return 1;
  }
  uw::FakeMcpServer server(command_line.HasSwitch("legacy"));
  std::string line;
  while (std::getline(std::cin, line)) {
    if (std::optional<std::string> answer = server.Answer(line)) {
      fprintf(stdout, "%s\n", answer->c_str());
      fflush(stdout);
    }
    if (server.crash()) {
      fprintf(stderr, "crashed on purpose\n");
      return 3;
    }
  }
  return 0;
}
