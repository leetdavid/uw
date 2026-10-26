// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_MEMORY_TOOLS_H_
#define UW_COMPONENTS_AGENT_MEMORY_TOOLS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace uw {

// Agent memory as the agent sees it: the tools that keep, forget, and search
// memories, and what the instructions say about them. See
// docs/plans/agent-memory.md.

// One memory: a plain sentence about the user.
struct Memory {
  int64_t id = 0;
  std::string text;
  // The agent reads it with every request.
  bool core = false;
  // The chat it came from, by id.
  std::string chat;
  base::Time changed;

  bool operator==(const Memory&) const = default;
};

inline constexpr char kRememberTool[] = "remember";
inline constexpr char kForgetTool[] = "forget";
inline constexpr char kSearchMemoryTool[] = "search_memory";

inline constexpr size_t kMaxCoreMemories = 30;
inline constexpr size_t kMaxMemoryLength = 300;
// The most memories a search answers with.
inline constexpr size_t kMaxFoundMemories = 20;

// The memory tools, as a model is told about them. A chat that keeps
// memories gets all three; a scheduled run only searches.
base::ListValue MemoryToolSchemas(bool keeps);

// What the instructions say about memory, with the `core` memories.
std::string MemoryInstructions(const std::vector<Memory>& core, bool keeps);

// The words a search looks for in `text`: lowercase, each once, at most 10,
// none shorter than 2 characters.
std::vector<std::string> MemorySearchWords(std::string_view text);

// A memory as a tool's answer gives it.
base::DictValue MemoryToValue(const Memory& memory);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_MEMORY_TOOLS_H_
