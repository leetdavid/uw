// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/pins/pin_store.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/pins/pin_prefs.h"

namespace uw {
namespace {

const void* const kUserDataKey = &kUserDataKey;

}  // namespace

// static
PinStore* PinStore::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* store = static_cast<PinStore*>(profile->GetUserData(kUserDataKey));
  if (!store) {
    auto made = std::make_unique<PinStore>(profile->GetPrefs());
    store = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return store;
}

PinStore::PinStore(PrefService* prefs) : prefs_(prefs) {
  for (const base::Value& saved : prefs_->GetList(kPinsPref)) {
    const base::DictValue* dict = saved.GetIfDict();
    const std::string* id = dict ? dict->FindString("id") : nullptr;
    const std::string* home = dict ? dict->FindString("home") : nullptr;
    if (id && home && GURL(*home).is_valid() && !Find(*id)) {
      pins_.push_back({*id, GURL(*home)});
    }
  }
}

PinStore::~PinStore() = default;

const PinStore::Pin* PinStore::Find(std::string_view id) const {
  const auto it = std::ranges::find(pins_, id, &Pin::id);
  return it == pins_.end() ? nullptr : &*it;
}

std::string PinStore::Insert(size_t index, const GURL& home) {
  std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  pins_.insert(pins_.begin() + std::min(index, pins_.size()), Pin{id, home});
  Changed();
  return id;
}

void PinStore::Remove(std::string_view id) {
  if (std::erase_if(pins_, [id](const Pin& pin) { return pin.id == id; })) {
    Changed();
  }
}

void PinStore::Reorder(const std::vector<std::string>& ids) {
  std::vector<Pin> ordered;
  for (const std::string& id : ids) {
    if (const Pin* pin = Find(id);
        pin && !std::ranges::contains(ordered, id, &Pin::id)) {
      ordered.push_back(*pin);
    }
  }
  for (const Pin& pin : pins_) {
    if (!std::ranges::contains(ordered, pin.id, &Pin::id)) {
      ordered.push_back(pin);
    }
  }
  if (std::ranges::equal(ordered, pins_, {}, &Pin::id, &Pin::id)) {
    return;
  }
  pins_ = std::move(ordered);
  Changed();
}

void PinStore::SetHome(std::string_view id, const GURL& home) {
  const auto it = std::ranges::find(pins_, id, &Pin::id);
  if (it == pins_.end() || !home.is_valid() || it->home == home) {
    return;
  }
  it->home = home;
  Changed();
}

base::CallbackListSubscription PinStore::Observe(
    base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}

void PinStore::Changed() {
  base::ListValue saved;
  for (const Pin& pin : pins_) {
    saved.Append(
        base::DictValue().Set("id", pin.id).Set("home", pin.home.spec()));
  }
  prefs_->SetList(kPinsPref, std::move(saved));
  if (notify_pending_) {
    return;
  }
  notify_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PinStore::Notify, weak_factory_.GetWeakPtr()));
}

void PinStore::Notify() {
  notify_pending_ = false;
  observers_.Notify();
}

}  // namespace uw
