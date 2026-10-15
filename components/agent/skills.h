// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_SKILLS_H_
#define UW_COMPONENTS_AGENT_SKILLS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace uw {

// A skill: instructions in a SKILL.md file, in the format Claude Code uses,
// that teach uw's agent one kind of request. See docs/CONTEXT.md.
struct Skill {
  enum class Source { kUw, kAgents, kChat };

  std::string name;
  std::string description;
  // The skill's SKILL.md. Its folder may hold more files the skill refers to.
  base::FilePath file;
  Source source = Source::kUw;
};

// The name and description between the "---" lines that open a SKILL.md.
// Nothing when either is missing.
struct SkillFrontMatter {
  std::string name;
  std::string description;
  // Written by uw when the agent saved the skill from a chat.
  bool made_in_chat = false;
};
std::optional<SkillFrontMatter> ReadSkillFrontMatter(std::string_view text);

// What follows the front matter: the skill's instructions.
std::string_view SkillInstructions(std::string_view text);

// A skill's name: 1 to 64 lowercase letters, digits, and hyphens.
bool IsSkillName(std::string_view name);

// The skills in the folders inside `folder`, sorted by name. `agents_folder`
// marks ~/.agents/skills, which other agents share. Blocks.
std::vector<Skill> FindSkills(const base::FilePath& folder, bool agents_folder);

// Writes a skill the agent made in a chat, as `folder`/`name`/SKILL.md.
// False when the skill exists or cannot be written. Blocks.
bool WriteSkill(const base::FilePath& folder,
                std::string_view name,
                std::string_view description,
                std::string_view instructions);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_SKILLS_H_
