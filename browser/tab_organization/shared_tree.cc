// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/tab_organization/shared_tree.h"

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/page_copy.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {

BASE_FEATURE(kUwSharedTabs, base::FEATURE_DISABLED_BY_DEFAULT);

namespace {

std::map<std::string, SharedTree*>& Trees() {
  static base::NoDestructor<std::map<std::string, SharedTree*>> trees;
  return *trees;
}

// What a navigation that started in a copy asked for, to do in the live page
// instead.
struct Redo {
  bool reload = false;
  int offset = 0;
  GURL url;
  ui::PageTransition transition = ui::PAGE_TRANSITION_LINK;
};

void RedoInLivePage(base::WeakPtr<content::WebContents> page, Redo redo) {
  tabs::TabInterface* tab =
      page ? tabs::TabInterface::MaybeGetFromContents(page.get()) : nullptr;
  TabOrganizationController* window =
      tab ? TabOrganizationController::From(tab->GetBrowserWindowInterface())
          : nullptr;
  if (!window || !SharedTree::IsCopy(page.get())) {
    return;
  }
  page->Stop();
  window->BringLivePage(tab);
  content::WebContents* live = tab->GetContents();
  if (live == page.get() || SharedTree::IsCopy(live)) {
    return;
  }
  content::NavigationController& controller = live->GetController();
  if (redo.reload) {
    controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
  } else if (redo.offset) {
    if (controller.CanGoToOffset(redo.offset)) {
      controller.GoToOffset(redo.offset);
    }
  } else if (redo.url.is_valid()) {
    controller.LoadURL(redo.url, content::Referrer(), redo.transition,
                       std::string());
  }
}

// Marks a page as a copy. A navigation that starts in one, such as Reload or
// Back, stops there and runs in the live page.
class CopyMark : public content::WebContentsUserData<CopyMark>,
                 public content::WebContentsObserver {
 public:
  ~CopyMark() override = default;

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument()) {
      return;
    }
    Redo redo;
    redo.reload = handle->GetReloadType() != content::ReloadType::NONE;
    redo.offset = handle->GetNavigationEntryOffset();
    redo.url = handle->GetURL();
    redo.transition = handle->GetPageTransition();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&RedoInLivePage, web_contents()->GetWeakPtr(), redo));
  }

 private:
  friend WebContentsUserData;
  explicit CopyMark(content::WebContents* page)
      : WebContentsUserData(*page), content::WebContentsObserver(page) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(CopyMark);

}  // namespace

SharedTree::SharedTree() = default;

SharedTree::~SharedTree() {
  if (!workspace_.empty()) {
    Trees().erase(workspace_);
  }
}

// static
scoped_refptr<SharedTree> SharedTree::For(const std::string& workspace) {
  SharedTree*& listed = Trees()[workspace];
  if (listed) {
    return base::WrapRefCounted(listed);
  }
  scoped_refptr<SharedTree> made = base::MakeRefCounted<SharedTree>();
  made->workspace_ = workspace;
  listed = made.get();
  return made;
}

void SharedTree::Add(TabOrganizationController* window) {
  windows_.push_back(window);
}

void SharedTree::Remove(TabOrganizationController* window) {
  std::erase(windows_, window);
}

void SharedTree::Merge(SharedTree& other) {
  std::vector<Page> pages;
  for (bool in_task : {true, false}) {
    for (const PageTree* from : {&tree_, &other.tree_}) {
      for (const Page& page : from->pages()) {
        if (page.task.empty() != in_task &&
            (from == &tree_ || !tree_.Find(page.id))) {
          pages.push_back(page);
        }
      }
    }
  }
  tree_ = PageTree(std::move(pages));
  collapsed_.insert(other.collapsed_.begin(), other.collapsed_.end());
  kept_.insert(other.kept_.begin(), other.kept_.end());
  held_.insert(other.held_.begin(), other.held_.end());
  generated_names_.insert(other.generated_names_.begin(),
                          other.generated_names_.end());
}

void SharedTree::Propagate(TabOrganizationController* from) {
  if (windows_.size() < 2 || propagating_ ||
      std::ranges::any_of(
          windows_, [](const auto& window) { return window->dragging(); })) {
    return;
  }
  base::AutoReset<bool> guard(&propagating_, true);
  // A window that follows can settle the tree a little; the rest follow that.
  for (int round = 0; round < 3 && tree_ != propagated_; ++round) {
    propagated_ = tree_;
    const std::vector<TabOrganizationController*> windows(windows_.begin(),
                                                          windows_.end());
    for (TabOrganizationController* window : windows) {
      if (window != from) {
        window->Follow();
      }
    }
    from = nullptr;
  }
}

void SharedTree::Touch() {
  std::erase_if(pictures_,
                [this](const auto& entry) { return !tree_.Find(entry.first); });
  for (TabOrganizationController* window : windows_) {
    window->ScheduleOwnUpdate();
  }
}

std::optional<tab_groups::TabGroupVisualData> SharedTree::VisualsOf(
    const std::string& task,
    const TabOrganizationController* except) const {
  for (TabOrganizationController* window : windows_) {
    if (window == except) {
      continue;
    }
    if (std::optional<tab_groups::TabGroupVisualData> visuals =
            window->VisualsOf(task)) {
      return visuals;
    }
  }
  return std::nullopt;
}

void SharedTree::SetVisuals(TabOrganizationController* from,
                            const std::string& task,
                            const tab_groups::TabGroupVisualData& visuals) {
  for (TabOrganizationController* window : windows_) {
    if (window != from) {
      window->SetTaskVisuals(task, visuals);
    }
  }
}

// static
std::unique_ptr<content::WebContents> SharedTree::MakeCopy(
    content::WebContents* live) {
  std::unique_ptr<content::WebContents> copy =
      CopyHistory(live, /*needs_reload=*/false);
  CopyMark::CreateForWebContents(copy.get());
  return copy;
}

// static
bool SharedTree::IsCopy(content::WebContents* page) {
  return page && CopyMark::FromWebContents(page);
}

content::WebContents* SharedTree::PageOf(
    const PageId& id,
    const TabOrganizationController* except) const {
  content::WebContents* found = nullptr;
  for (TabOrganizationController* window : windows_) {
    tabs::TabInterface* tab = window == except ? nullptr : window->TabFor(id);
    if (!tab) {
      continue;
    }
    if (!IsCopy(tab->GetContents())) {
      return tab->GetContents();
    }
    found = found ? found : tab->GetContents();
  }
  return found;
}

TabOrganizationController* SharedTree::HolderOf(
    const PageId& id,
    const TabOrganizationController* except) const {
  for (TabOrganizationController* window : windows_) {
    tabs::TabInterface* tab = window == except ? nullptr : window->TabFor(id);
    if (tab && !IsCopy(tab->GetContents())) {
      return window;
    }
  }
  return nullptr;
}

void SharedTree::BringLivePage(TabOrganizationController* window,
                               const PageId& id) {
  tabs::TabInterface* here = window->TabFor(id);
  if (!here || !IsCopy(here->GetContents())) {
    return;
  }
  if (TabOrganizationController* holder = HolderOf(id, window)) {
    tabs::TabInterface* there = holder->TabFor(id);
    content::WebContents* live = there->GetContents();
    TakePicture(live, base::BindOnce(&SharedTree::SetPicture,
                                     weak_factory_.GetWeakPtr(), id));
    window->ReplacePage(here, holder->ReplacePage(there, MakeCopy(live)));
    return;
  }
  // No window holds it, so the copy loads its history and becomes it.
  content::WebContents* page = here->GetContents();
  page->RemoveUserData(CopyMark::UserDataKey());
  page->GetController().SetNeedsReload();
  page->GetController().LoadIfNecessary();
}

void SharedTree::ScheduleRefresh(const PageId& id) {
  if (windows_.size() < 2 || !refreshing_.insert(id).second) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SharedTree::Refresh, weak_factory_.GetWeakPtr(), id),
      base::Milliseconds(300));
}

void SharedTree::Refresh(const PageId& id) {
  refreshing_.erase(id);
  TabOrganizationController* holder = HolderOf(id, nullptr);
  if (!holder) {
    return;
  }
  content::WebContents* live = holder->TabFor(id)->GetContents();
  for (TabOrganizationController* window : windows_) {
    tabs::TabInterface* tab = window == holder ? nullptr : window->TabFor(id);
    if (tab && IsCopy(tab->GetContents()) &&
        !SameHistory(tab->GetContents(), live)) {
      window->ReplacePage(tab, MakeCopy(live));
    }
  }
}

gfx::ImageSkia SharedTree::PictureOf(const PageId& id) const {
  const auto picture = pictures_.find(id);
  return picture == pictures_.end() ? gfx::ImageSkia() : picture->second;
}

void SharedTree::SetPicture(const PageId& id, gfx::ImageSkia picture) {
  pictures_[id] = std::move(picture);
  for (TabOrganizationController* window : windows_) {
    window->NotifyPictures();
  }
}

}  // namespace uw
