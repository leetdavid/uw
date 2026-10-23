// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_window.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tabs/public/tab_interface.h"
#include "components/viz/common/frame_timing_details.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/compositor.h"
#include "url/gurl.h"
#include "uw/browser/workspaces/workspace_list.h"

namespace uw {
namespace {

// How long a window revealed under its partner waits to draw before it comes
// to the front anyway.
constexpr base::TimeDelta kRevealWait = base::Milliseconds(200);

base::RepeatingCallbackList<void(BrowserWindowInterface*)>& Reveals() {
  static base::NoDestructor<
      base::RepeatingCallbackList<void(BrowserWindowInterface*)>>
      reveals;
  return *reveals;
}

std::string Find(const std::map<std::string, std::string>& data,
                 const char* key) {
  const auto it = data.find(key);
  return it == data.end() ? std::string() : it->second;
}

void DeleteProfile(const base::FilePath& profile) {
  ProfileManager* manager = g_browser_process->profile_manager();
  manager->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
      manager->user_data_dir().Append(profile), base::DoNothing(),
      ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
}

// Drops every closed window of `workspace` from Cmd+Shift+T.
void ForgetClosedWindows(Profile* profile, std::string_view workspace) {
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfileIfExisting(profile);
  if (!service) {
    return;
  }
  std::vector<SessionID> entries;
  for (const auto& entry : service->entries()) {
    if (entry->type == sessions::tab_restore::Type::WINDOW &&
        Find(entry->extra_data, WorkspaceWindow::kWorkspaceKey) == workspace) {
      entries.push_back(entry->id);
    }
  }
  for (SessionID id : entries) {
    service->RemoveEntryById(id);
  }
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
}

void WorkspaceWindow::Start() {
  close_subscription_ = window_->RegisterBrowserDidClose(
      base::BindRepeating(&WorkspaceWindow::OnClosed, base::Unretained(this)));
  if (views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
          window_->GetWindow()->GetNativeWindow())) {
    widget_observation_.Observe(widget);
  }
  SaveSession();
}

base::CallbackListSubscription WorkspaceWindow::ObserveChanges(
    base::RepeatingClosure changed) {
  return changes_.Add(std::move(changed));
}

WorkspaceWindow::~WorkspaceWindow() = default;

// static
WorkspaceWindow* WorkspaceWindow::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

// static
bool WorkspaceWindow::IsHidden(BrowserWindowInterface* window) {
  const WorkspaceWindow* self = From(window);
  return self && self->hidden_;
}

// static
void WorkspaceWindow::ShowSavedIcon(content::WebContents* page) {
  if (favicon::ContentFaviconDriver* icons =
          favicon::ContentFaviconDriver::FromWebContents(page)) {
    icons->FetchFavicon(icons->GetActiveURL(), /*is_same_document=*/false);
  }
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

bool WorkspaceWindow::Switch(std::string_view id, WindowCallback shown) {
  if (hidden_ || switching() || !WorkspaceList::Get().Find(id)) {
    return false;
  }
  WithPartner(id, base::BindOnce(
                      [](WindowCallback shown, BrowserWindowInterface* window) {
                        From(window)->Reveal(
                            shown ? base::BindOnce(std::move(shown), window)
                                  : base::OnceClosure());
                      },
                      std::move(shown)));
  return true;
}

bool WorkspaceWindow::switching() const {
  if (revealing_) {
    return true;
  }
  for (const WorkspaceWindow* partner : Partners()) {
    if (partner->revealing_) {
      return true;
    }
  }
  return false;
}

void WorkspaceWindow::Slide(std::string_view id) {
  if (slide_) {
    slide_.Run(std::string(id));
    return;
  }
  Switch(id);
}

void WorkspaceWindow::SetSlide(
    base::RepeatingCallback<void(const std::string&)> slide) {
  slide_ = std::move(slide);
}

void WorkspaceWindow::WithPartner(std::string_view id, WindowCallback found) {
  if (id == workspace_) {
    std::move(found).Run(window_);
    return;
  }
  for (WorkspaceWindow* partner : Partners()) {
    if (partner->workspace_ == id) {
      std::move(found).Run(partner->window_);
      return;
    }
  }
  const WorkspaceList::Workspace* workspace = WorkspaceList::Get().Find(id);
  if (!workspace) {
    return;
  }
  ProfileManager* manager = g_browser_process->profile_manager();
  manager->LoadProfileByPath(
      manager->user_data_dir().Append(workspace->profile),
      /*incognito=*/false,
      base::BindOnce(&WorkspaceWindow::OpenPartner, weak_factory_.GetWeakPtr(),
                     workspace->id, std::move(found)));
}

void WorkspaceWindow::Preload(std::string_view id) {
  WithPartner(id, base::BindOnce([](BrowserWindowInterface* window) {
                From(window)->LoadActiveTab();
              }));
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
    if (std::string group = Find(extra_data, kGroupKey); !group.empty()) {
      self->group_ = std::move(group);
    }
    self->SetHidden(Find(extra_data, kHiddenKey) == "true");
  } else {
    // The window of a deleted workspace comes back on its own.
    self->workspace_ = WorkspaceList::Get().FirstFor(window->GetProfile()).id;
  }
  self->SaveSession();
  self->changes_.Notify();
  // A group shows one window. A window revealed before this one came back
  // goes behind it again.
  if (!self->hidden_) {
    for (WorkspaceWindow* partner : self->Partners()) {
      if (!partner->hidden_) {
        partner->SetHidden(true);
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

void WorkspaceWindow::OnWidgetVisibilityChanged(views::Widget* widget,
                                                bool visible) {
  // A reveal orders the window in under its partner itself.
  if (visible && hidden_ && !revealing_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceWindow::OnShownElsewhere,
                                  weak_factory_.GetWeakPtr()));
  }
}

void WorkspaceWindow::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

void WorkspaceWindow::OnShownElsewhere() {
  if (!hidden_ || revealing_ || !window_->GetWindow()->IsVisible()) {
    return;
  }
  WillShow(window_);
  // A window being restored goes behind its partner again.
  if (hidden_) {
    window_->GetWindow()->Hide();
  }
}

// static
base::CallbackListSubscription WorkspaceWindow::ObserveReveals(
    base::RepeatingCallback<void(BrowserWindowInterface*)> shown) {
  return Reveals().Add(std::move(shown));
}

void WorkspaceWindow::Reveal(base::OnceClosure revealed) {
  if (revealed) {
    revealed_.push_back(std::move(revealed));
  }
  if (revealing_) {
    return;
  }
  WorkspaceWindow* shown = nullptr;
  for (WorkspaceWindow* partner : Partners()) {
    if (!partner->hidden_) {
      shown = partner;
    }
  }
  // With nothing to swap with, or once something else has put the window on
  // screen, it comes to the front at once.
  if (!shown || window_->GetWindow()->IsVisible()) {
    Present(shown ? shown->weak_factory_.GetWeakPtr() : nullptr);
    return;
  }
  // Otherwise it goes in just under its partner, where it draws unseen, and
  // comes to the front once it has drawn. Neither window plays macOS's open
  // or close animation.
  revealing_ = true;
  ++reveals_;
  SetAnimates(false);
  shown->SetAnimates(false);
  window_->GetWindow()->SetBounds(shown->window_->GetWindow()->GetBounds());
  SetContentsHidden(false);
  OrderBelow(shown);
  LoadActiveTab();
  const auto present = base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindow> self,
         base::WeakPtr<WorkspaceWindow> shown, int reveal) {
        if (self && self->revealing_ && self->reveals_ == reveal) {
          self->Present(shown);
        }
      },
      weak_factory_.GetWeakPtr(), shown->weak_factory_.GetWeakPtr(),
      reveals_);
  // The page draws first, then the window shows a frame with it.
  const auto drawn = base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindow> self, base::RepeatingClosure present,
         int reveal) {
        if (!self || !self->revealing_ || self->reveals_ != reveal) {
          return;
        }
        views::Widget* widget = self->GetWidget();
        ui::Compositor* compositor =
            widget ? widget->GetCompositor() : nullptr;
        if (!compositor) {
          present.Run();
          return;
        }
        compositor->RequestSuccessfulPresentationTimeForNextFrame(
            base::BindOnce([](base::RepeatingClosure present,
                              const viz::FrameTimingDetails&) {
              present.Run();
            }, present));
      },
      weak_factory_.GetWeakPtr(), present, reveals_);
  content::WebContents* page =
      window_->GetTabStripModel()->GetActiveWebContents();
  if (page && page->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    page->GetPrimaryMainFrame()->InsertVisualStateCallback(base::BindOnce(
        [](base::RepeatingClosure drawn, bool) { drawn.Run(); }, drawn));
  } else {
    drawn.Run();
  }
  reveal_timer_.Start(FROM_HERE, kRevealWait, present);
}

void WorkspaceWindow::Present(base::WeakPtr<WorkspaceWindow> shown) {
  revealing_ = false;
  reveal_timer_.Stop();
  SetHidden(false);
  SaveSession();
  if (shown && shown.get() != this) {
    window_->GetWindow()->SetBounds(shown->window_->GetWindow()->GetBounds());
  }
  window_->GetWindow()->Show();
  LoadActiveTab();
  if (shown && shown.get() != this && !shown->hidden_) {
    shown->SetAnimates(false);
    shown->SetHidden(true);
    shown->SaveSession();
    shown->window_->GetWindow()->Hide();
    shown->SetAnimates(true);
  }
  SetAnimates(true);
  Reveals().Notify(window_);
  std::vector<base::OnceClosure> revealed;
  revealed.swap(revealed_);
  for (base::OnceClosure& done : revealed) {
    std::move(done).Run();
  }
}

void WorkspaceWindow::LoadActiveTab() {
  if (content::WebContents* active =
          window_->GetTabStripModel()->GetActiveWebContents()) {
    active->GetController().LoadIfNecessary();
  }
}

views::Widget* WorkspaceWindow::GetWidget() {
  return views::Widget::GetWidgetForNativeWindow(
      window_->GetWindow()->GetNativeWindow());
}

void WorkspaceWindow::SetAnimates(bool animates) {
  if (views::Widget* widget = GetWidget()) {
    widget->SetVisibilityChangedAnimationsEnabled(animates);
  }
}

void WorkspaceWindow::OpenPartner(const std::string& id,
                                  WindowCallback found,
                                  Profile* profile) {
  if (!profile || window_->IsDeleteScheduled()) {
    return;
  }
  // A second switch may have opened it while the profile loaded.
  for (WorkspaceWindow* partner : Partners()) {
    if (partner->workspace_ == id) {
      std::move(found).Run(partner->window_);
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
  partner->SetHidden(true);
  partner->SaveSession();
  partner->changes_.Notify();
  NavigateParams new_tab(opened, GURL(chrome::kChromeUINewTabURL),
                         ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  new_tab.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&new_tab);
  std::move(found).Run(opened);
}

// static
void WorkspaceWindow::Create(BrowserWindowInterface* window,
                             std::string name,
                             std::string icon,
                             const base::FilePath& profile,
                             const std::u16string& new_profile,
                             WindowCallback shown) {
  WorkspaceWindow* self = From(window);
  if (!self) {
    return;
  }
  auto add = base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> self, std::string name,
         std::string icon, WindowCallback shown, Profile* profile) {
        if (!self || !profile) {
          return;
        }
        const std::string id = WorkspaceList::Get().Add(
            std::move(name), std::move(icon), profile->GetBaseName());
        self->Switch(id, std::move(shown));
      },
      self->weak_factory_.GetWeakPtr(), std::move(name), std::move(icon),
      std::move(shown));
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!profile.empty()) {
    manager->LoadProfileByPath(manager->user_data_dir().Append(profile),
                               /*incognito=*/false, std::move(add));
    return;
  }
  ProfileManager::CreateMultiProfileAsync(new_profile,
                                          profiles::GetPlaceholderAvatarIndex(),
                                          /*is_hidden=*/false, std::move(add));
}

// static
void WorkspaceWindow::Delete(std::string_view id, bool with_profile) {
  WorkspaceList& list = WorkspaceList::Get();
  const int index = list.IndexOf(id);
  if (index < 0 || list.workspaces().size() < 2) {
    return;
  }
  const std::string deleted(id);
  const base::FilePath profile = list.workspaces()[index].profile;
  const std::string before = list.workspaces()[index > 0 ? index - 1 : 1].id;
  list.Remove(deleted);
  for (Profile* loaded :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    ForgetClosedWindows(loaded, deleted);
  }

  std::vector<WorkspaceWindow*> shown;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    WorkspaceWindow* self = From(window);
    if (!self || self->workspace_ != deleted || window->IsDeleteScheduled()) {
      continue;
    }
    self->forget_ = true;
    if (self->hidden_) {
      window->GetWindow()->Close();
    } else {
      shown.push_back(self);
    }
  }
  // The profile goes once every window that showed the workspace has
  // switched away, so no window of another workspace closes with it.
  base::OnceClosure after = base::DoNothing();
  if (with_profile) {
    after = base::BindOnce(&DeleteProfile, profile);
  }
  const base::RepeatingClosure switched =
      base::BarrierClosure(shown.size(), std::move(after));
  for (WorkspaceWindow* self : shown) {
    self->Switch(before, base::BindOnce(
                             [](base::WeakPtr<WorkspaceWindow> self,
                                base::RepeatingClosure switched,
                                BrowserWindowInterface*) {
                               if (self) {
                                 self->window_->GetWindow()->Close();
                               }
                               switched.Run();
                             },
                             self->weak_factory_.GetWeakPtr(), switched));
  }
}

// static
int WorkspaceWindow::CountTabs(std::string_view id) {
  int tabs = 0;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    const WorkspaceWindow* self = From(window);
    if (self && self->workspace_ == id) {
      const TabStripModel* strip = window->GetTabStripModel();
      tabs += strip->count() - strip->IndexOfFirstNonPinnedTab();
    }
  }
  return tabs;
}

// static
void WorkspaceWindow::MoveTab(tabs::TabInterface* tab, std::string_view id) {
  WorkspaceWindow* self = From(tab->GetBrowserWindowInterface());
  if (!self || tab->IsPinned() || id == self->workspace_) {
    return;
  }
  self->WithPartner(
      id,
      base::BindOnce(
          [](base::WeakPtr<tabs::TabInterface> tab,
             BrowserWindowInterface* to) {
            if (!tab) {
              return;
            }
            BrowserWindowInterface* from = tab->GetBrowserWindowInterface();
            TabStripModel* source = from->GetTabStripModel();
            // The window keeps a tab, so it stays open.
            if (source->count() == 1) {
              NavigateParams new_tab(from, GURL(chrome::kChromeUINewTabURL),
                                     ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
              new_tab.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
              Navigate(&new_tab);
            }
            const int index = source->GetIndexOfTab(tab.get());
            TabStripModel* target = to->GetTabStripModel();
            if (to->GetProfile() == from->GetProfile()) {
              target->InsertDetachedTabAt(
                  target->count(), source->DetachTabAtForInsertion(index),
                  AddTabTypes::ADD_ACTIVE);
              return;
            }
            NavigateParams params(to, tab->GetContents()->GetLastCommittedURL(),
                                  ui::PAGE_TRANSITION_LINK);
            params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
            Navigate(&params);
            source->CloseWebContentsAt(index,
                                       TabCloseTypes::CLOSE_USER_GESTURE);
          },
          tab->GetWeakPtr()));
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
  if (forget_) {
    ForgetClosedWindows(window->GetProfile(), workspace_);
    return;
  }
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
