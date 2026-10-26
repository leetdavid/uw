// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_list.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/workspaces/workspace_prefs.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

}  // namespace

// static
const std::vector<std::string_view>& WorkspaceList::SuggestedIcons() {
  // clang-format off
  static const base::NoDestructor<std::vector<std::string_view>> icons({
      "💼", "🏠", "🧪", "📚", "🎨", "🎵", "✈️", "💡",
      "🛒", "🏃", "🌱", "🎮", "🍳", "💰", "🎓", "❤️",
  });
  // clang-format on
  return *icons;
}

// static
WorkspaceList& WorkspaceList::Get() {
  static base::NoDestructor<WorkspaceList> list(
      g_browser_process->local_state(), g_browser_process->profile_manager());
  return *list;
}

WorkspaceList::WorkspaceList(PrefService* local_state,
                             ProfileManager* profile_manager)
    : local_state_(local_state), profile_manager_(profile_manager) {
  manager_observation_.Observe(profile_manager_);
  ProfileAttributesStorage& profiles =
      profile_manager_->GetProfileAttributesStorage();
  profiles_observation_.Observe(&profiles);

  for (const base::Value& saved : local_state_->GetList(kWorkspacesPref)) {
    const base::DictValue* dict = saved.GetIfDict();
    const std::string* id = dict ? dict->FindString("id") : nullptr;
    const std::string* name = dict ? dict->FindString("name") : nullptr;
    const std::string* icon = dict ? dict->FindString("icon") : nullptr;
    const std::string* profile = dict ? dict->FindString("profile") : nullptr;
    if (id && name && icon && profile && !Find(*id)) {
      const base::DictValue* theme = dict->FindDict("theme");
      workspaces_.push_back({*id, *name, *icon,
                             base::FilePath::FromUTF8Unsafe(*profile),
                             theme ? ThemeFromValue(*theme) : std::nullopt});
    }
  }
  if (!workspaces_.empty()) {
    return;
  }
  for (const ProfileAttributesEntry* entry :
       profiles.GetAllProfilesAttributesSortedByNameWithCheck()) {
    workspaces_.push_back({NewId(), base::UTF16ToUTF8(entry->GetName()),
                           std::string(UnusedIcon()),
                           entry->GetPath().BaseName()});
  }
  Save();
}

WorkspaceList::~WorkspaceList() = default;

const WorkspaceList::Workspace* WorkspaceList::Find(std::string_view id) const {
  const auto it = std::ranges::find(workspaces_, id, &Workspace::id);
  return it == workspaces_.end() ? nullptr : &*it;
}

WorkspaceList::Workspace* WorkspaceList::FindMutable(std::string_view id) {
  const auto it = std::ranges::find(workspaces_, id, &Workspace::id);
  return it == workspaces_.end() ? nullptr : &*it;
}

int WorkspaceList::IndexOf(std::string_view id) const {
  const auto it = std::ranges::find(workspaces_, id, &Workspace::id);
  return it == workspaces_.end() ? -1 : it - workspaces_.begin();
}

std::vector<const WorkspaceList::Workspace*> WorkspaceList::ForProfile(
    const base::FilePath& profile) const {
  std::vector<const Workspace*> found;
  for (const Workspace& workspace : workspaces_) {
    if (workspace.profile == profile) {
      found.push_back(&workspace);
    }
  }
  return found;
}

const WorkspaceList::Workspace& WorkspaceList::FirstFor(
    const Profile* profile) {
  const base::FilePath base_name = profile->GetBaseName();
  if (const auto it =
          std::ranges::find(workspaces_, base_name, &Workspace::profile);
      it != workspaces_.end()) {
    return *it;
  }
  Add(ProfileName(base_name), std::string(UnusedIcon()), base_name);
  return workspaces_.back();
}

std::string WorkspaceList::Add(std::string name,
                               std::string icon,
                               const base::FilePath& profile) {
  std::string id = NewId();
  workspaces_.push_back({id, std::move(name), std::move(icon), profile});
  Changed();
  return id;
}

void WorkspaceList::Rename(std::string_view id, std::string name) {
  if (Workspace* workspace = FindMutable(id);
      workspace && workspace->name != name) {
    workspace->name = std::move(name);
    Changed();
  }
}

void WorkspaceList::SetIcon(std::string_view id, std::string icon) {
  if (Workspace* workspace = FindMutable(id);
      workspace && workspace->icon != icon) {
    workspace->icon = std::move(icon);
    Changed();
  }
}

void WorkspaceList::SetTheme(std::string_view id, const Theme& theme) {
  if (Workspace* workspace = FindMutable(id);
      workspace && workspace->theme != theme) {
    workspace->theme = theme;
    Changed();
  }
}

void WorkspaceList::Remove(std::string_view id) {
  if (std::erase_if(workspaces_, [id](const Workspace& workspace) {
        return workspace.id == id;
      })) {
    Changed();
  }
}

std::string_view WorkspaceList::UnusedIcon() const {
  const std::vector<std::string_view>& icons = SuggestedIcons();
  const auto unused =
      std::ranges::find_if(icons, [this](std::string_view icon) {
        return !std::ranges::contains(workspaces_, icon, &Workspace::icon);
      });
  return unused == icons.end() ? icons.front() : *unused;
}

base::CallbackListSubscription WorkspaceList::Observe(
    base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}

void WorkspaceList::OnProfileWasRemoved(const base::FilePath& profile_path,
                                        const std::u16string& profile_name) {
  if (std::erase_if(workspaces_, [&](const Workspace& workspace) {
        return workspace.profile == profile_path.BaseName();
      })) {
    Changed();
  }
}

void WorkspaceList::OnProfileManagerDestroying() {
  profiles_observation_.Reset();
  manager_observation_.Reset();
  profile_manager_ = nullptr;
}

std::string WorkspaceList::ProfileName(const base::FilePath& profile) const {
  const ProfileAttributesEntry* entry =
      profile_manager_
          ? profile_manager_->GetProfileAttributesStorage()
                .GetProfileAttributesWithPath(
                    profile_manager_->user_data_dir().Append(profile))
          : nullptr;
  return entry ? base::UTF16ToUTF8(entry->GetName()) : profile.AsUTF8Unsafe();
}

void WorkspaceList::Save() {
  base::ListValue saved;
  for (const Workspace& workspace : workspaces_) {
    base::DictValue entry =
        base::DictValue()
            .Set("id", workspace.id)
            .Set("name", workspace.name)
            .Set("icon", workspace.icon)
            .Set("profile", workspace.profile.AsUTF8Unsafe());
    if (workspace.theme) {
      entry.Set("theme", ThemeToValue(*workspace.theme));
    }
    saved.Append(std::move(entry));
  }
  local_state_->SetList(kWorkspacesPref, std::move(saved));
}

void WorkspaceList::Changed() {
  Save();
  if (notify_pending_) {
    return;
  }
  notify_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceList::Notify, weak_factory_.GetWeakPtr()));
}

void WorkspaceList::Notify() {
  notify_pending_ = false;
  observers_.Notify();
}

}  // namespace uw
