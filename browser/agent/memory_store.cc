// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/memory_store.h"

#include <memory>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/types/expected_macros.h"
#include "chrome/browser/profiles/profile.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "uw/components/agent/check_questions.h"

namespace uw {
namespace {

const void* const kUserDataKey = &kUserDataKey;

constexpr char kFile[] = "uw_memory";
constexpr int kVersion = 1;

constexpr char kColumns[] = "id, text, core, chat, changed";

Memory Read(sql::Statement& statement) {
  Memory memory;
  memory.id = statement.ColumnInt64(0);
  memory.text = statement.ColumnString(1);
  memory.core = statement.ColumnBool(2);
  memory.chat = statement.ColumnString(3);
  memory.changed = statement.ColumnTime(4);
  return memory;
}

// `word` as a LIKE pattern that finds it anywhere in a memory.
std::string Anywhere(std::string_view word) {
  std::string escaped;
  for (char c : word) {
    if (c == '\\' || c == '%' || c == '_') {
      escaped += '\\';
    }
    escaped += c;
  }
  return base::StrCat({"%", escaped, "%"});
}

}  // namespace

class MemoryStore::Backend {
 public:
  explicit Backend(base::FilePath path)
      : path_(std::move(path)), db_(sql::Database::Tag("UwMemory")) {}
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;
  ~Backend() = default;

  std::vector<Memory> Core() {
    std::vector<Memory> core;
    if (!Ready()) {
      return core;
    }
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, text, core, chat, changed FROM memories WHERE core = 1 "
        "ORDER BY id"));
    while (statement.Step()) {
      core.push_back(Read(statement));
    }
    return core;
  }

  std::pair<base::expected<KeepResult, std::string>, std::vector<Memory>>
  Keep(std::string text, bool core, std::string chat, int64_t replaces) {
    base::expected<KeepResult, std::string> kept =
        KeepNow(std::move(text), core, std::move(chat), replaces);
    return {std::move(kept), Core()};
  }

  std::pair<std::optional<Memory>, std::vector<Memory>> Forget(int64_t id) {
    std::optional<Memory> memory = Ready() ? Find(id) : std::nullopt;
    if (memory) {
      sql::Statement statement(db_.GetCachedStatement(
          SQL_FROM_HERE, "DELETE FROM memories WHERE id = ?"));
      statement.BindInt64(0, id);
      if (!statement.Run()) {
        memory.reset();
      }
    }
    return {std::move(memory), Core()};
  }

  size_t Count() {
    if (!Ready()) {
      return 0;
    }
    sql::Statement statement(
        db_.GetCachedStatement(SQL_FROM_HERE, "SELECT COUNT(*) FROM memories"));
    return statement.Step() ? static_cast<size_t>(statement.ColumnInt64(0)) : 0;
  }

  std::pair<base::expected<Memory, std::string>, std::vector<Memory>> Edit(
      int64_t id,
      std::string text) {
    base::expected<Memory, std::string> edited = EditNow(id, std::move(text));
    return {std::move(edited), Core()};
  }

  std::vector<Memory> ForgetAll() {
    if (Ready()) {
      std::ignore = db_.Execute("DELETE FROM memories");
    }
    return Core();
  }

  std::vector<Memory> Search(std::vector<std::string> words, size_t limit) {
    std::vector<Memory> found;
    if (!Ready()) {
      return found;
    }
    std::string sql;
    if (words.empty()) {
      sql = base::StrCat({"SELECT ", kColumns,
                          " FROM memories ORDER BY changed DESC, id DESC "
                          "LIMIT ?"});
    } else {
      const std::vector<std::string> hits(words.size(),
                                          "(text LIKE ? ESCAPE '\\')");
      sql = base::StrCat({"SELECT ", kColumns, ", ",
                          base::JoinString(hits, " + "),
                          " AS hits FROM memories WHERE hits > 0 ORDER BY "
                          "hits DESC, changed DESC, id DESC LIMIT ?"});
    }
    sql::Statement statement(db_.GetUniqueStatement(sql));
    int index = 0;
    for (const std::string& word : words) {
      statement.BindString(index++, Anywhere(word));
    }
    statement.BindInt64(index, static_cast<int64_t>(limit));
    while (statement.Step()) {
      found.push_back(Read(statement));
    }
    return found;
  }

 private:
  // Opens the database and makes its table the first time.
  bool Ready() {
    if (db_.is_open()) {
      return true;
    }
    if (!db_.Open(path_)) {
      return false;
    }
    sql::MetaTable meta;
    if (!meta.Init(&db_, kVersion, kVersion) ||
        !db_.Execute("CREATE TABLE IF NOT EXISTS memories("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "text TEXT NOT NULL,"
                     "core INTEGER NOT NULL,"
                     "chat TEXT NOT NULL,"
                     "changed INTEGER NOT NULL)")) {
      db_.Close();
      return false;
    }
    return true;
  }

  std::optional<Memory> Find(int64_t id) {
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, text, core, chat, changed FROM memories WHERE id = ?"));
    statement.BindInt64(0, id);
    return statement.Step() ? std::optional(Read(statement)) : std::nullopt;
  }

  std::optional<Memory> FindText(std::string_view text) {
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, text, core, chat, changed FROM memories WHERE text = ?"));
    statement.BindString(0, text);
    return statement.Step() ? std::optional(Read(statement)) : std::nullopt;
  }

  size_t CoreCount() {
    sql::Statement statement(db_.GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM memories WHERE core = 1"));
    return statement.Step() ? static_cast<size_t>(statement.ColumnInt64(0)) : 0;
  }

  // `text` on one line, or what is wrong with it as a memory.
  static base::expected<std::string, std::string> Sentence(std::string text) {
    text = OneLine(text, text.size());
    if (text.empty()) {
      return base::unexpected("A memory needs a sentence.");
    }
    if (text.size() > kMaxMemoryLength) {
      return base::unexpected(
          "A memory is one sentence of at most 300 characters.");
    }
    return base::ok(std::move(text));
  }

  base::expected<Memory, std::string> EditNow(int64_t id, std::string text) {
    ASSIGN_OR_RETURN(text, Sentence(std::move(text)));
    std::optional<Memory> memory = Ready() ? Find(id) : std::nullopt;
    if (!memory) {
      return base::unexpected("That memory is gone.");
    }
    if (const std::optional<Memory> same = FindText(text);
        same && same->id != id) {
      return base::unexpected("Another memory says that already.");
    }
    memory->text = std::move(text);
    memory->changed = base::Time::Now();
    sql::Statement update(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE memories SET text = ?, changed = ? WHERE id = ?"));
    update.BindString(0, memory->text);
    update.BindTime(1, memory->changed);
    update.BindInt64(2, id);
    if (!update.Run()) {
      return base::unexpected("Memory can't be changed right now.");
    }
    return *memory;
  }

  base::expected<KeepResult, std::string> KeepNow(std::string text,
                                                  bool core,
                                                  std::string chat,
                                                  int64_t replaces) {
    ASSIGN_OR_RETURN(text, Sentence(std::move(text)));
    if (!Ready()) {
      return base::unexpected("Memory can't be opened right now.");
    }
    sql::Transaction transaction(&db_);
    if (!transaction.Begin()) {
      return base::unexpected("Memory can't be changed right now.");
    }
    const std::optional<Memory> old = replaces ? Find(replaces) : std::nullopt;
    if (replaces && !old) {
      return base::unexpected(base::StrCat(
          {"There is no memory ", base::NumberToString(replaces), "."}));
    }
    const std::optional<Memory> same = FindText(text);
    if (core) {
      size_t others = CoreCount();
      others -= old && old->core ? 1 : 0;
      others -= same && same->core && (!old || same->id != old->id) ? 1 : 0;
      if (others >= kMaxCoreMemories) {
        return base::unexpected(
            "There are already 30 core memories. Keep this one not core, or "
            "first make another not core: remember it again with core false "
            "and its id as replaces.");
      }
    }
    if (old && (!same || same->id != old->id)) {
      sql::Statement remove(db_.GetCachedStatement(
          SQL_FROM_HERE, "DELETE FROM memories WHERE id = ?"));
      remove.BindInt64(0, old->id);
      if (!remove.Run()) {
        return base::unexpected("Memory can't be changed right now.");
      }
    }
    Memory memory;
    memory.text = std::move(text);
    memory.core = core;
    memory.chat = std::move(chat);
    memory.changed = base::Time::Now();
    sql::Statement write(
        same ? db_.GetCachedStatement(
                   SQL_FROM_HERE,
                   "UPDATE memories SET core = ?, chat = ?, changed = ? "
                   "WHERE id = ?")
             : db_.GetCachedStatement(
                   SQL_FROM_HERE,
                   "INSERT INTO memories (core, chat, changed, text) "
                   "VALUES (?, ?, ?, ?)"));
    write.BindBool(0, memory.core);
    write.BindString(1, memory.chat);
    write.BindTime(2, memory.changed);
    if (same) {
      write.BindInt64(3, same->id);
    } else {
      write.BindString(3, memory.text);
    }
    if (!write.Run() || !transaction.Commit()) {
      return base::unexpected("Memory can't be changed right now.");
    }
    memory.id = same ? same->id : db_.GetLastInsertRowId();
    KeepResult result;
    result.kept = std::move(memory);
    if (old && old->id != result.kept.id) {
      result.replaced = old;
    }
    return result;
  }

  const base::FilePath path_;
  sql::Database db_;
};

// static
MemoryStore* MemoryStore::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* store = static_cast<MemoryStore*>(profile->GetUserData(kUserDataKey));
  if (!store) {
    auto made = std::make_unique<MemoryStore>(profile->GetPath());
    store = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return store;
}

MemoryStore::MemoryStore(const base::FilePath& profile_path)
    : backend_(base::ThreadPool::CreateSequencedTaskRunner(
                   {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                    base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
               profile_path.AppendASCII(kFile)) {
  backend_.AsyncCall(&Backend::Core)
      .Then(base::BindOnce(&MemoryStore::OnLoaded, weak_factory_.GetWeakPtr()));
}

MemoryStore::~MemoryStore() = default;

void MemoryStore::WhenLoaded(base::OnceClosure done) {
  if (loaded_) {
    std::move(done).Run();
    return;
  }
  waiting_.push_back(std::move(done));
}

void MemoryStore::Keep(std::string text,
                       bool core,
                       std::string chat,
                       int64_t replaces,
                       Kept done) {
  backend_.AsyncCall(&Backend::Keep)
      .WithArgs(std::move(text), core, std::move(chat), replaces)
      .Then(base::BindOnce(&MemoryStore::OnKept, weak_factory_.GetWeakPtr(),
                           std::move(done)));
}

void MemoryStore::Forget(int64_t id, Forgot done) {
  backend_.AsyncCall(&Backend::Forget)
      .WithArgs(id)
      .Then(base::BindOnce(&MemoryStore::OnForgot, weak_factory_.GetWeakPtr(),
                           std::move(done)));
}

void MemoryStore::Search(std::vector<std::string> words,
                         size_t limit,
                         Found done) {
  backend_.AsyncCall(&Backend::Search)
      .WithArgs(std::move(words), limit)
      .Then(std::move(done));
}

void MemoryStore::Count(base::OnceCallback<void(size_t)> done) {
  backend_.AsyncCall(&Backend::Count).Then(std::move(done));
}

void MemoryStore::Edit(int64_t id, std::string text, Edited done) {
  backend_.AsyncCall(&Backend::Edit)
      .WithArgs(id, std::move(text))
      .Then(base::BindOnce(&MemoryStore::OnEdited, weak_factory_.GetWeakPtr(),
                           std::move(done)));
}

void MemoryStore::ForgetAll(base::OnceClosure done) {
  backend_.AsyncCall(&Backend::ForgetAll)
      .Then(base::BindOnce(&MemoryStore::OnForgotAll,
                           weak_factory_.GetWeakPtr(), std::move(done)));
}

void MemoryStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MemoryStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void MemoryStore::OnLoaded(std::vector<Memory> core) {
  core_ = std::move(core);
  loaded_ = true;
  for (base::OnceClosure& done : std::exchange(waiting_, {})) {
    std::move(done).Run();
  }
}

void MemoryStore::OnKept(Kept done,
                         std::pair<base::expected<KeepResult, std::string>,
                                   std::vector<Memory>> kept) {
  const bool changed = kept.first.has_value();
  core_ = std::move(kept.second);
  std::move(done).Run(std::move(kept.first));
  if (changed) {
    Changed(core_);
  }
}

void MemoryStore::OnForgot(
    Forgot done,
    std::pair<std::optional<Memory>, std::vector<Memory>> forgot) {
  const bool changed = forgot.first.has_value();
  core_ = std::move(forgot.second);
  std::move(done).Run(std::move(forgot.first));
  if (changed) {
    Changed(core_);
  }
}

void MemoryStore::OnEdited(Edited done,
                           std::pair<base::expected<Memory, std::string>,
                                     std::vector<Memory>> edited) {
  const bool changed = edited.first.has_value();
  core_ = std::move(edited.second);
  std::move(done).Run(std::move(edited.first));
  if (changed) {
    Changed(core_);
  }
}

void MemoryStore::OnForgotAll(base::OnceClosure done,
                              std::vector<Memory> core) {
  std::move(done).Run();
  Changed(std::move(core));
}

void MemoryStore::Changed(std::vector<Memory> core) {
  core_ = std::move(core);
  for (Observer& observer : observers_) {
    observer.OnMemoriesChanged();
  }
}

}  // namespace uw
