// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/pins/pin_pages.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/web_contents.h"

namespace uw {
namespace {

const void* const kUserDataKey = &kUserDataKey;

}  // namespace

// static
PinPages* PinPages::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* pages = static_cast<PinPages*>(profile->GetUserData(kUserDataKey));
  if (!pages) {
    auto made = std::make_unique<PinPages>(profile);
    pages = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return pages;
}

PinPages::PinPages(Profile* profile) : profile_(profile) {
  profile_observation_.Observe(profile);
  if (ProfileManager* manager = g_browser_process->profile_manager()) {
    manager_observation_.Observe(manager);
  }
  terminating_ = browser_shutdown::AddAppTerminatingCallback(
      base::BindOnce(&PinPages::DropAll, base::Unretained(this)));
}

PinPages::~PinPages() = default;

void PinPages::Hold(std::string id,
                    std::unique_ptr<content::WebContents> page) {
  page->WasHidden();
  pages_[std::move(id)] = std::move(page);
  if (!keep_alive_) {
    keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
        profile_, ProfileKeepAliveOrigin::kUwPinnedPage);
  }
}

std::unique_ptr<content::WebContents> PinPages::Take(std::string_view id) {
  const auto it = pages_.find(id);
  if (it == pages_.end()) {
    return nullptr;
  }
  std::unique_ptr<content::WebContents> page = std::move(it->second);
  pages_.erase(it);
  if (pages_.empty()) {
    keep_alive_.reset();
  }
  return page;
}

bool PinPages::Holds(std::string_view id) const {
  return pages_.contains(id);
}

void PinPages::OnProfileWillBeDestroyed(Profile* profile) {
  DropAll();
  profile_observation_.Reset();
}

void PinPages::OnProfileMarkedForPermanentDeletion(Profile* profile) {
  if (profile == profile_) {
    DropAll();
  }
}

void PinPages::OnProfileManagerDestroying() {
  manager_observation_.Reset();
}

void PinPages::DropAll() {
  pages_.clear();
  keep_alive_.reset();
}

}  // namespace uw
