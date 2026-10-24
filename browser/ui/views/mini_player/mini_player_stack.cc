// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_stack.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

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
  std::erase_if(paused_, [contents](const Entry& entry) {
    return !entry.contents || entry.contents.get() == contents;
  });
  if (hidden_ && hidden_->contents && hidden_->contents.get() != contents) {
    content::MediaSession::Get(hidden_->contents.get())
        ->Suspend(content::MediaSession::SuspendType::kUI);
    paused_.insert(paused_.begin(), std::move(*hidden_));
    if (paused_.size() > kMaxPaused) {
      paused_.resize(kMaxPaused);
    }
  }
  hidden_.reset();
  observers_.Notify();
}

void MiniPlayerStack::Play(size_t index) {
  if (index >= paused_.size()) {
    return;
  }
  base::WeakPtr<content::WebContents> contents = paused_[index].contents;
  paused_.erase(paused_.begin() + index);
  observers_.Notify();
  if (!contents) {
    return;
  }
  content::MediaSession* session = content::MediaSession::Get(contents.get());
  session->Resume(content::MediaSession::SuspendType::kUI);
  session->EnterPictureInPicture();
}

void MiniPlayerStack::Drop(size_t index) {
  if (index < paused_.size()) {
    paused_.erase(paused_.begin() + index);
    observers_.Notify();
  }
}

void MiniPlayerStack::Remove(content::WebContents* contents) {
  if (std::erase_if(paused_, [contents](const Entry& entry) {
        return entry.contents.get() == contents;
      })) {
    observers_.Notify();
  }
}

base::CallbackListSubscription MiniPlayerStack::Observe(
    base::RepeatingClosure callback) {
  return observers_.Add(std::move(callback));
}

void MiniPlayerStack::ForgetHidden() {
  hidden_.reset();
}

}  // namespace uw
