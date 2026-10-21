// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_window.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/common/webui_url_constants.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "uw/browser/workspaces/workspace_list.h"

namespace uw {
namespace {

std::string Find(const std::map<std::string, std::string>& data,
                 const char* key) {
  const auto it = data.find(key);
  return it == data.end() ? std::string() : it->second;
}

bool IsRestoring(Profile* profile) {
  if (SessionRestore::IsRestoring(profile)) {
    return true;
  }
  const sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfileIfExisting(profile);
  return service && service->IsRestoring();
}

}  // namespace

DEFINE_USER_DATA(WorkspaceWindow);

WorkspaceWindow::WorkspaceWindow(BrowserWindowInterface* window)
    : window_(window),
      group_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  // A new window shows the workspace of the window in front when they share
  // a profile, as after Cmd+N, and otherwise its profile's first one.
  BrowserWindowInterface* front =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  const WorkspaceWindow* in_front = From(front);
  workspace_ = in_front && front->GetProfile() == window->GetProfile()
                   ? in_front->workspace_
                   : WorkspaceList::Get().FirstFor(window->GetProfile()).id;
  close_subscription_ = window->RegisterBrowserDidClose(
      base::BindRepeating(&WorkspaceWindow::OnClosed, base::Unretained(this)));
  SaveSession();
}

WorkspaceWindow::~WorkspaceWindow() = default;

// static
WorkspaceWindow* WorkspaceWindow::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

std::vector<WorkspaceWindow*> WorkspaceWindow::Partners() const {
  std::vector<WorkspaceWindow*> partners;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    WorkspaceWindow* other = From(window);
    if (other && other != this && other->group_ == group_ &&
        !window->IsDeleteScheduled()) {
      partners.push_back(other);
    }
  }
  return partners;
}

void WorkspaceWindow::Switch(std::string_view id) {
  const WorkspaceList::Workspace* workspace = WorkspaceList::Get().Find(id);
  if (!workspace || hidden_ || id == workspace_) {
    return;
  }
  for (WorkspaceWindow* partner : Partners()) {
    if (partner->workspace_ == id) {
      partner->Reveal();
      return;
    }
  }
  ProfileManager* manager = g_browser_process->profile_manager();
  manager->LoadProfileByPath(
      manager->user_data_dir().Append(workspace->profile),
      /*incognito=*/false,
      base::BindOnce(&WorkspaceWindow::OpenPartner, weak_factory_.GetWeakPtr(),
                     workspace->id));
}

// static
void WorkspaceWindow::Restore(
    BrowserWindowInterface* window,
    const std::map<std::string, std::string>& extra_data) {
  WorkspaceWindow* self = From(window);
  if (!self) {
    return;
  }
  const WorkspaceList::Workspace* workspace =
      WorkspaceList::Get().Find(Find(extra_data, kWorkspaceKey));
  if (workspace && workspace->profile == window->GetProfile()->GetBaseName()) {
    self->workspace_ = workspace->id;
  }
  if (std::string group = Find(extra_data, kGroupKey); !group.empty()) {
    self->group_ = std::move(group);
  }
  self->hidden_ = Find(extra_data, kHiddenKey) == "true";
  self->SaveSession();
  // A group shows one window. A window revealed before this one came back
  // goes behind it again.
  if (!self->hidden_) {
    for (WorkspaceWindow* partner : self->Partners()) {
      if (!partner->hidden_) {
        partner->hidden_ = true;
        partner->SaveSession();
        partner->window_->GetWindow()->Hide();
      }
    }
  }

  const sessions::TabRestoreService* tab_restore =
      TabRestoreServiceFactory::GetForProfileIfExisting(window->GetProfile());
  if (tab_restore && tab_restore->IsRestoring()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceWindow::RestorePartners,
                                  self->weak_factory_.GetWeakPtr()));
    return;
  }
  static base::NoDestructor<base::CallbackListSubscription> restored(
      SessionRestore::RegisterOnSessionRestoredCallback(
          base::BindRepeating([](Profile*, int) {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(&WorkspaceWindow::RevealOrphans));
          })));
}

// static
void WorkspaceWindow::AddRestoreData(
    BrowserWindowInterface* window,
    std::map<std::string, std::string>& extra_data) {
  if (const WorkspaceWindow* self = From(window)) {
    extra_data[kWorkspaceKey] = self->workspace_;
    extra_data[kGroupKey] = self->group_;
    extra_data[kHiddenKey] = self->hidden_ ? "true" : "false";
  }
}

// static
bool WorkspaceWindow::ClosesWith(BrowserWindowInterface* window,
                                 SessionID closing) {
  const WorkspaceWindow* self = From(window);
  return self && self->hidden_ &&
         std::ranges::any_of(
             self->Partners(), [closing](WorkspaceWindow* partner) {
               return !partner->hidden_ &&
                      partner->window_->GetSessionID() == closing;
             });
}

// static
bool WorkspaceWindow::WillShow(BrowserWindowInterface* window) {
  WorkspaceWindow* self = From(window);
  if (!self || !self->hidden_) {
    return true;
  }
  if (!IsRestoring(window->GetProfile())) {
    self->Reveal();
  }
  return false;
}

void WorkspaceWindow::Reveal() {
  WorkspaceWindow* shown = nullptr;
  for (WorkspaceWindow* partner : Partners()) {
    if (!partner->hidden_) {
      shown = partner;
    }
  }
  hidden_ = false;
  SaveSession();
  if (shown) {
    window_->GetWindow()->SetBounds(shown->window_->GetWindow()->GetBounds());
  }
  window_->GetWindow()->Show();
  if (shown) {
    shown->hidden_ = true;
    shown->SaveSession();
    shown->window_->GetWindow()->Hide();
  }
}

void WorkspaceWindow::OpenPartner(const std::string& id, Profile* profile) {
  if (!profile || hidden_ || window_->IsDeleteScheduled()) {
    return;
  }
  // A second switch may have opened it while the profile loaded.
  for (WorkspaceWindow* partner : Partners()) {
    if (partner->workspace_ == id) {
      partner->Reveal();
      return;
    }
  }
  BrowserWindowCreateParams params(*profile, /*from_user_gesture=*/true);
  params.initial_bounds = window_->GetWindow()->GetBounds();
  BrowserWindowInterface* opened = CreateBrowserWindow(std::move(params));
  WorkspaceWindow* partner = From(opened);
  if (!partner) {
    return;
  }
  partner->workspace_ = id;
  partner->group_ = group_;
  partner->hidden_ = true;
  NavigateParams new_tab(opened, GURL(chrome::kChromeUINewTabURL),
                         ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  new_tab.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&new_tab);
  partner->Reveal();
}

void WorkspaceWindow::SaveSession() {
  SessionService* service =
      SessionServiceFactory::GetForProfile(window_->GetProfile());
  if (!service) {
    return;
  }
  std::map<std::string, std::string> data;
  AddRestoreData(window_, data);
  for (const auto& [key, value] : data) {
    service->AddWindowExtraData(window_->GetSessionID(), key.c_str(), value);
  }
}

// Closing the window that shows closes its whole group. Quitting leaves the
// group to the session, which brings it back at launch.
void WorkspaceWindow::OnClosed(BrowserWindowInterface* window) {
  if (hidden_ || browser_shutdown::IsTryingToQuit()) {
    return;
  }
  for (WorkspaceWindow* partner : Partners()) {
    partner->window_->GetWindow()->Close();
  }
}

void WorkspaceWindow::RestorePartners() {
  for (Profile* profile :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    sessions::TabRestoreService* service =
        TabRestoreServiceFactory::GetForProfileIfExisting(profile);
    if (!service) {
      continue;
    }
    std::vector<SessionID> partners;
    for (const auto& entry : service->entries()) {
      if (entry->type == sessions::tab_restore::Type::WINDOW &&
          Find(entry->extra_data, kGroupKey) == group_) {
        partners.push_back(entry->id);
      }
    }
    for (SessionID id : partners) {
      service->RestoreEntryById(nullptr, id, WindowOpenDisposition::NEW_WINDOW);
    }
  }
  RevealOrphans();
}

// static
void WorkspaceWindow::RevealOrphans() {
  std::set<std::string> shown;
  std::vector<WorkspaceWindow*> hidden;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (WorkspaceWindow* self = From(window);
        self && !window->IsDeleteScheduled()) {
      if (self->hidden_) {
        hidden.push_back(self);
      } else {
        shown.insert(self->group_);
      }
    }
  }
  for (WorkspaceWindow* self : hidden) {
    if (shown.insert(self->group_).second) {
      self->Reveal();
    }
  }
}

}  // namespace uw
