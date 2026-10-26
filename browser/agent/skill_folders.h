// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_SKILL_FOLDERS_H_
#define UW_BROWSER_AGENT_SKILL_FOLDERS_H_

#include <vector>

#include "base/files/file_path.h"
#include "uw/components/agent/skills.h"

namespace uw {

// uw's own skills, and those made in chats: beside the profiles.
base::FilePath UwSkillsFolder();

// The skills other agents share, ~/.agents/skills.
base::FilePath AgentsSkillsFolder();
// Reads the shared skills from `folder` instead. An empty path restores
// ~/.agents/skills.
void SetAgentsSkillsFolderForTesting(base::FilePath folder);

// The skills in both folders, uw's first; one in ~/.agents/skills with the
// name of one of uw's is left out. Blocks.
std::vector<Skill> FindAllSkills(base::FilePath uw, base::FilePath agents);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_SKILL_FOLDERS_H_
