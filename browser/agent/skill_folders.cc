// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/skill_folders.h"

#include <algorithm>
#include <utility>

#include "base/base_paths.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"

namespace uw {
namespace {

base::FilePath& AgentsSkillsFolderOverride() {
  static base::NoDestructor<base::FilePath> folder;
  return *folder;
}

}  // namespace

base::FilePath UwSkillsFolder() {
  base::FilePath data;
  base::PathService::Get(chrome::DIR_USER_DATA, &data);
  return data.AppendASCII("skills");
}

base::FilePath AgentsSkillsFolder() {
  if (!AgentsSkillsFolderOverride().empty()) {
    return AgentsSkillsFolderOverride();
  }
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home.AppendASCII(".agents").AppendASCII("skills");
}

void SetAgentsSkillsFolderForTesting(base::FilePath folder) {
  AgentsSkillsFolderOverride() = std::move(folder);
}

std::vector<Skill> FindAllSkills(base::FilePath uw, base::FilePath agents) {
  std::vector<Skill> skills = FindSkills(uw, /*agents_folder=*/false);
  for (Skill& skill : FindSkills(agents, /*agents_folder=*/true)) {
    if (std::ranges::none_of(skills, [&](const Skill& kept) {
          return kept.name == skill.name;
        })) {
      skills.push_back(std::move(skill));
    }
  }
  return skills;
}

}  // namespace uw
