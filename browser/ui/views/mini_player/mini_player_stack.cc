// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_stack.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

namespace uw {

MiniPlayerStack::Entry::Entry() = default;
MiniPlayerStack::Entry::Entry(const Entry&) = default;
MiniPlayerStack::Entry& MiniPlayerStack::Entry::operator=(const Entry&) =
    default;
MiniPlayerStack::Entry::~Entry() = default;

MiniPlayerStack::MiniPlayerStack() = default;
MiniPlayerStack::~MiniPlayerStack() = default;

// static
MiniPlayerStack& MiniPlayerStack::Get() {
  static base::NoDestructor<MiniPlayerStack> stack;
  return *stack;
}

void MiniPlayerStack::OnHidden(content::WebContents* contents,
                               gfx::ImageSkia still,
                               base::TimeDelta position) {
  Entry entry;
  entry.contents = contents->GetWeakPtr();
  entry.title = contents->GetTitle();
  if (content::NavigationEntry* page =
          contents->GetController().GetLastCommittedEntry()) {
    entry.favicon = page->GetFavicon().image.AsImageSkia();
  }
  entry.still = still.isNull() ? entry.favicon : still;
  entry.paused_at = position;
  hidden_ = std::move(entry);
  // Chromium hides the old mini player and shows the new one in one go. A
  // mini player that was only closed is forgotten once this task ends.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MiniPlayerStack::ForgetHidden,
                                weak_factory_.GetWeakPtr()));
}

void MiniPlayerStack::OnShown(content::WebContents* contents) {
  std::vector<Entry> gone;
  std::erase_if(paused_, [contents, &gone](const Entry& entry) {
    if (entry.contents && entry.contents.get() != contents) {
      return false;
    }
    gone.push_back(entry);
    return true;
  });
  if (hidden_ && hidden_->contents && hidden_->contents.get() != contents) {
    content::MediaSession::Get(hidden_->contents.get())
        ->Suspend(content::MediaSession::SuspendType::kUI);
    paused_.insert(paused_.begin(), std::move(*hidden_));
  }
  // The stack lives only in the sidebar. Without it the replaced video just
  // stays paused in its page.
  const size_t kept = MiniPlayerModeOf(g_browser_process->local_state()) ==
                              MiniPlayerMode::kDock
                          ? kMaxPaused
                          : 0;
  while (paused_.size() > kept) {
    gone.push_back(std::move(paused_.back()));
    paused_.pop_back();
  }
  hidden_.reset();
  observers_.Notify();
  for (const Entry& entry : gone) {
    Dropped(entry);
  }
}

void MiniPlayerStack::Play(size_t index) {
  if (index >= paused_.size()) {
    return;
  }
  const Entry entry = paused_[index];
  paused_.erase(paused_.begin() + index);
  observers_.Notify();
  if (entry.play_site_window) {
    // The video playing now goes under the site's window, paused.
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
    KeepHidden();
    entry.play_site_window.Run();
    return;
  }
  base::WeakPtr<content::WebContents> contents = entry.contents;
  if (!contents) {
    return;
  }
  content::MediaSession* session = content::MediaSession::Get(contents.get());
  session->Resume(content::MediaSession::SuspendType::kUI);
  session->EnterPictureInPicture();
}

void MiniPlayerStack::Drop(size_t index) {
  if (index < paused_.size()) {
    const Entry entry = paused_[index];
    paused_.erase(paused_.begin() + index);
    observers_.Notify();
    Dropped(entry);
  }
}

void MiniPlayerStack::Remove(content::WebContents* contents) {
  std::vector<Entry> removed;
  std::erase_if(paused_, [contents, &removed](const Entry& entry) {
    if (entry.contents.get() != contents) {
      return false;
    }
    removed.push_back(entry);
    return true;
  });
  if (removed.empty()) {
    return;
  }
  observers_.Notify();
  for (const Entry& entry : removed) {
    Dropped(entry);
  }
}

void MiniPlayerStack::PushSiteWindow(Entry entry) {
  paused_.insert(paused_.begin(), std::move(entry));
  if (paused_.size() > kMaxPaused) {
    Dropped(paused_.back());
    paused_.resize(kMaxPaused);
  }
  observers_.Notify();
}

void MiniPlayerStack::TakeSiteWindow(content::WebContents* contents) {
  if (std::erase_if(paused_, [contents](const Entry& entry) {
        return entry.play_site_window && entry.contents.get() == contents;
      })) {
    observers_.Notify();
  }
}

void MiniPlayerStack::KeepHidden() {
  if (!hidden_ || !hidden_->contents) {
    hidden_.reset();
    return;
  }
  content::MediaSession::Get(hidden_->contents.get())
      ->Suspend(content::MediaSession::SuspendType::kUI);
  paused_.insert(paused_.begin(), std::move(*hidden_));
  hidden_.reset();
  if (paused_.size() > kMaxPaused) {
    Dropped(paused_.back());
    paused_.resize(kMaxPaused);
  }
  observers_.Notify();
}

// static
void MiniPlayerStack::Dropped(const Entry& entry) {
  if (entry.drop_site_window) {
    entry.drop_site_window.Run();
  }
}

base::CallbackListSubscription MiniPlayerStack::Observe(
    base::RepeatingClosure callback) {
  return observers_.Add(std::move(callback));
}

void MiniPlayerStack::ForgetHidden() {
  // Nothing showed in its place: the mini player was closed, or its tab was.
  if (hidden_) {
    hidden_.reset();
    BringUpNewest();
  }
}

void MiniPlayerStack::BringUpNewest() {
  // A site's window waiting here plays again by itself.
  auto newest = std::ranges::find_if(paused_, [](const Entry& entry) {
    return entry.contents && !entry.play_site_window;
  });
  if (newest == paused_.end()) {
    return;
  }
  base::WeakPtr<content::WebContents> contents = newest->contents;
  paused_.erase(newest);
  observers_.Notify();
  content::MediaSession::Get(contents.get())->EnterPictureInPicture();
}

}  // namespace uw
