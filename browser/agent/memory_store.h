// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_MEMORY_STORE_H_
#define UW_BROWSER_AGENT_MEMORY_STORE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/threading/sequence_bound.h"
#include "base/types/expected.h"
#include "uw/components/agent/memory_tools.h"

class Profile;

namespace uw {

// The agent memory of one profile: plain sentences about the user, in
// uw_memory, a SQLite database in the profile folder. It is read and written
// on a background sequence; the core memories are also kept here, so a
// request can carry them at once. See docs/plans/agent-memory.md and
// ADR 0018.
class MemoryStore : public base::SupportsUserData::Data {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A memory was kept, reworded, or forgotten.
    virtual void OnMemoriesChanged() = 0;
  };

  // The memory of `profile`, made on first use. None in a private profile,
  // which keeps no memories.
  static MemoryStore* For(Profile* profile);

  explicit MemoryStore(const base::FilePath& profile_path);
  MemoryStore(const MemoryStore&) = delete;
  MemoryStore& operator=(const MemoryStore&) = delete;
  ~MemoryStore() override;

  // Runs `done` once the core memories are read, at once when they are.
  void WhenLoaded(base::OnceClosure done);
  // The core memories, oldest first. Empty until they are read.
  const std::vector<Memory>& core() const { return core_; }

  // Keeps `text`, from the chat `chat`, in place of the memory `replaces`
  // when that is not 0. The same sentence again is kept once. `done` gets
  // the memory kept and the one it replaced, or what is wrong, as the agent
  // is told.
  struct KeepResult {
    Memory kept;
    std::optional<Memory> replaced;
  };
  using Kept =
      base::OnceCallback<void(base::expected<KeepResult, std::string>)>;
  void Keep(std::string text,
            bool core,
            std::string chat,
            int64_t replaces,
            Kept done);
  // Forgets the memory `id`. `done` gets it, or nothing when there is none.
  using Forgot = base::OnceCallback<void(std::optional<Memory>)>;
  void Forget(int64_t id, Forgot done);
  // The memories that contain any of `words`, the most matches first, then
  // the newest; for no words, every memory, newest first. At most `limit`.
  using Found = base::OnceCallback<void(std::vector<Memory>)>;
  void Search(std::vector<std::string> words, size_t limit, Found done);
  // How many memories there are.
  void Count(base::OnceCallback<void(size_t)> done);
  // Rewords the memory `id` as the user typed it. `done` gets the memory,
  // or what is wrong.
  using Edited = base::OnceCallback<void(base::expected<Memory, std::string>)>;
  void Edit(int64_t id, std::string text, Edited done);
  // Forgets every memory.
  void ForgetAll(base::OnceClosure done);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  class Backend;

  void OnLoaded(std::vector<Memory> core);
  void OnKept(Kept done,
              std::pair<base::expected<KeepResult, std::string>,
                        std::vector<Memory>> kept);
  void OnForgot(Forgot done,
                std::pair<std::optional<Memory>, std::vector<Memory>> forgot);
  void OnEdited(Edited done,
                std::pair<base::expected<Memory, std::string>,
                          std::vector<Memory>> edited);
  void OnForgotAll(base::OnceClosure done, std::vector<Memory> core);
  // Takes the core memories after a change, and tells the observers.
  void Changed(std::vector<Memory> core);

  base::SequenceBound<Backend> backend_;
  bool loaded_ = false;
  std::vector<Memory> core_;
  std::vector<base::OnceClosure> waiting_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<MemoryStore> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_MEMORY_STORE_H_
