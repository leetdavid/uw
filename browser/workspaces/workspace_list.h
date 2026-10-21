// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_LIST_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_LIST_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_attributes_storage_observer.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

class PrefService;
class Profile;
class ProfileAttributesStorage;
class ProfileManager;

namespace uw {

// Every workspace, in the switcher's order, with the profile each belongs to.
// Kept in local state, since every window's switcher lists every profile's
// workspaces. At first use, each profile becomes a workspace with its name.
class WorkspaceList : public ProfileAttributesStorageObserver,
                      public ProfileManagerObserver {
 public:
  struct Workspace {
    std::string id;
    std::string name;
    // An emoji.
    std::string icon;
    // The profile's directory name, under the user data directory.
    base::FilePath profile;
  };

  // The icons the new workspace card offers, in its order.
  static const std::vector<std::string_view>& SuggestedIcons();

  static WorkspaceList& Get();

  WorkspaceList(PrefService* local_state, ProfileManager* profile_manager);
  WorkspaceList(const WorkspaceList&) = delete;
  WorkspaceList& operator=(const WorkspaceList&) = delete;
  ~WorkspaceList() override;

  const std::vector<Workspace>& workspaces() const { return workspaces_; }
  const Workspace* Find(std::string_view id) const;
  // Where `id` is in the switcher, or -1.
  int IndexOf(std::string_view id) const;
  std::vector<const Workspace*> ForProfile(const base::FilePath& profile) const;
  // The first workspace of `profile`. A profile with none gets one, named
  // after the profile.
  const Workspace& FirstFor(const Profile* profile);

  // Each change saves the list and tells the observers on a later task.
  std::string Add(std::string name,
                  std::string icon,
                  const base::FilePath& profile);
  void Rename(std::string_view id, std::string name);
  void SetIcon(std::string_view id, std::string icon);
  void Remove(std::string_view id);

  // The first suggested icon no workspace uses, or the first one.
  std::string_view UnusedIcon() const;

  base::CallbackListSubscription Observe(base::RepeatingClosure changed);

  // ProfileAttributesStorageObserver:
  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const std::u16string& profile_name) override;

  // ProfileManagerObserver:
  void OnProfileManagerDestroying() override;

 private:
  Workspace* FindMutable(std::string_view id);
  std::string ProfileName(const base::FilePath& profile) const;
  void Save();
  void Changed();
  void Notify();

  const raw_ptr<PrefService> local_state_;
  raw_ptr<ProfileManager> profile_manager_;
  std::vector<Workspace> workspaces_;
  bool notify_pending_ = false;
  base::RepeatingClosureList observers_;
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorageObserver>
      profiles_observation_{this};
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      manager_observation_{this};
  base::WeakPtrFactory<WorkspaceList> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_LIST_H_
