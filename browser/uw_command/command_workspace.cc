// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/uw_command/command_workspace.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "ui/base/base_window.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {

namespace {

// The workspace of the front window that shows one, as the switcher does.
std::optional<CommandWorkspace> FrontWorkspace() {
  std::optional<CommandWorkspace> front;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        const WorkspaceWindow* shown = WorkspaceWindow::From(window);
        if (!shown || shown->hidden() ||
            window->GetProfile()->IsOffTheRecord()) {
          return true;
        }
        front = CommandWorkspace{shown->workspace(),
                                 window->GetProfile()->GetPath()};
        return false;
      });
  return front;
}

}  // namespace

base::expected<CommandWorkspace, std::string> ChooseWorkspace(
    const base::DictValue& params) {
  ProfileManager* profiles = g_browser_process->profile_manager();
  const std::string* name = params.FindString("workspace");
  if (!name) {
    return FrontWorkspace().value_or(
        CommandWorkspace{"", profiles->GetLastUsedProfileDir()});
  }
  std::vector<const WorkspaceList::Workspace*> named;
  for (const WorkspaceList::Workspace& workspace :
       WorkspaceList::Get().workspaces()) {
    if (base::EqualsCaseInsensitiveASCII(workspace.name, *name)) {
      named.push_back(&workspace);
    }
  }
  if (named.size() == 1) {
    return CommandWorkspace{named[0]->id,
                            profiles->user_data_dir().Append(named[0]->profile)};
  }
  if (named.empty()) {
    return base::unexpected(
        base::StrCat({"No workspace is called ", *name, ". Workspaces: ",
                      base::JoinString(WorkspaceNames(nullptr), ", "), "."}));
  }
  return base::unexpected(base::StrCat(
      {"More than one workspace is called ", *name, ". Rename one in uw."}));
}

void LoadWorkspace(CommandWorkspace workspace, WorkspaceLoaded loaded) {
  g_browser_process->profile_manager()->LoadProfileByPath(
      workspace.profile, /*incognito=*/false,
      base::BindOnce(
          [](std::string id, WorkspaceLoaded loaded, Profile* profile) {
            if (profile && id.empty()) {
              id = WorkspaceList::Get().FirstFor(profile).id;
            }
            std::move(loaded).Run(profile, id);
          },
          std::move(workspace.id), std::move(loaded)));
}

BrowserWindowInterface* FrontWindow(Profile* profile) {
  BrowserWindowInterface* front = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        if (window->GetProfile() == profile &&
            window->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            !IsRunWindow(window) && !WorkspaceWindow::IsHidden(window)) {
          front = window;
          return false;
        }
        return true;
      });
  return front;
}

BrowserWindowInterface* ShowFrontWindow(Profile* profile) {
  BrowserWindowInterface* front = FrontWindow(profile);
  if (!front) {
    front = CreateBrowserWindow(
        BrowserWindowCreateParams(*profile, /*from_user_gesture=*/true));
  }
  if (front) {
    front->GetWindow()->Show();
    front->GetWindow()->Activate();
  }
  return front;
}

std::vector<std::string> WorkspaceNames(const Profile* profile) {
  std::vector<std::string> names;
  for (const WorkspaceList::Workspace& workspace :
       WorkspaceList::Get().workspaces()) {
    if (!profile || workspace.profile == profile->GetBaseName()) {
      names.push_back(workspace.name);
    }
  }
  return names;
}

}  // namespace uw
