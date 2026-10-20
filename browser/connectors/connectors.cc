// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/connectors.h"

#include <algorithm>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"
#include "chrome/common/chrome_paths.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/connectors/local_transport.h"
#include "uw/browser/connectors/remote_transport.h"
#include "uw/components/terminal/shell_host.h"
#include "uw/components/terminal/shell_quote.h"

namespace uw {
namespace {

constexpr char kListFile[] = "connectors.json";
// A tool call may wait on the user's other apps, or on a slow page.
constexpr base::TimeDelta kCallTimeout = base::Minutes(10);
// A connector that stops after it was ready starts again this many times
// before it shows as failed.
constexpr int kRestarts = 1;

std::string SecretsAccount(std::string_view name) {
  return base::StrCat({"connector:", name});
}

base::ListValue ToList(const std::set<std::string>& names) {
  base::ListValue list;
  for (const std::string& name : names) {
    list.Append(name);
  }
  return list;
}

std::set<std::string> FromList(const base::ListValue* list) {
  std::set<std::string> names;
  if (list) {
    for (const base::Value& name : *list) {
      if (name.is_string()) {
        names.insert(name.GetString());
      }
    }
  }
  return names;
}

// The list and each connector's saved tools, read off the main thread.
struct Saved {
  std::optional<base::DictValue> list;
  std::map<std::string, base::DictValue> caches;
};

Saved ReadSaved(const base::FilePath& folder) {
  Saved saved;
  std::string text;
  if (!base::ReadFileToString(folder.AppendASCII(kListFile), &text)) {
    return saved;
  }
  saved.list = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  const base::ListValue* connectors =
      saved.list ? saved.list->FindList("connectors") : nullptr;
  if (!connectors) {
    return saved;
  }
  for (const base::Value& item : *connectors) {
    const std::string* name =
        item.is_dict() ? item.GetDict().FindString("name") : nullptr;
    if (!name || !IsConnectorName(*name)) {
      continue;
    }
    std::string cache;
    if (base::ReadFileToString(folder.AppendASCII(*name + ".json"), &cache)) {
      if (std::optional<base::DictValue> dict =
              base::JSONReader::ReadDict(cache, base::JSON_PARSE_RFC)) {
        saved.caches[*name] = std::move(*dict);
      }
    }
  }
  return saved;
}

void WriteFile(base::FilePath path, std::string text) {
  base::CreateDirectory(path.DirName());
  base::ImportantFileWriter::WriteFileAtomically(path, text);
}

void DeleteFiles(base::FilePath folder, std::string name) {
  base::DeleteFile(folder.AppendASCII(name + ".json"));
  base::DeleteFile(folder.AppendASCII(name + ".log"));
}

// Where the user's own shell is: the terminal tab's.
base::FilePath UserShell() {
  return ShellHost::ForUser().shell;
}

}  // namespace

// A connector that is starting or running.
struct Connectors::Live {
  int generation = 0;
  std::unique_ptr<McpClient> client;
  bool ready = false;
  // Requests made while it starts, run once it is ready or has failed.
  std::vector<base::OnceClosure> waiting;
};

Connectors::Info::Info() = default;
Connectors::Info::Info(const Info&) = default;
Connectors::Info& Connectors::Info::operator=(const Info&) = default;
Connectors::Info::~Info() = default;

// static
Connectors& Connectors::Get() {
  static base::NoDestructor<Connectors> connectors;
  return *connectors;
}

Connectors::Connectors()
    : files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  base::FilePath data;
  base::PathService::Get(chrome::DIR_USER_DATA, &data);
  folder_ = data.AppendASCII("connectors");
}

Connectors::~Connectors() = default;

// static
void Connectors::SetUpForTesting(const base::FilePath& folder,
                                 const base::FilePath& shell) {
  Connectors& connectors = Get();
  connectors.live_.clear();
  connectors.list_.clear();
  connectors.eras_.clear();
  connectors.restarts_.clear();
  connectors.started_ = false;
  connectors.read_ = false;
  connectors.folder_ = folder;
  connectors.shell_ = shell;
  connectors.loader_ = nullptr;
}

void Connectors::Start(scoped_refptr<network::SharedURLLoaderFactory> loader) {
  if (started_) {
    return;
  }
  started_ = true;
  loader_ = std::move(loader);
  if (shell_.empty()) {
    shell_ = UserShell();
  }
  files_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadSaved, folder_),
      base::BindOnce(
          [](base::WeakPtr<Connectors> self, Saved saved) {
            if (self) {
              self->OnRead(std::move(saved.list), std::move(saved.caches));
            }
          },
          weak_factory_.GetWeakPtr()));
}

void Connectors::OnRead(std::optional<base::DictValue> saved,
                        std::map<std::string, base::DictValue> caches) {
  read_ = true;
  const base::ListValue* connectors =
      saved ? saved->FindList("connectors") : nullptr;
  if (!connectors) {
    Changed();
    return;
  }
  for (const base::Value& item : *connectors) {
    const base::DictValue* dict = item.GetIfDict();
    std::optional<ConnectorConfig> config =
        dict ? ConnectorFromValue(*dict) : std::nullopt;
    if (!config || FindInfo(config->name)) {
      continue;
    }
    Info info;
    info.config = std::move(*config);
    info.on = dict->FindBool("on").value_or(true);
    info.tools_off = FromList(dict->FindList("tools_off"));
    info.always_allowed = FromList(dict->FindList("always_allowed"));
    if (auto cache = caches.find(info.config.name); cache != caches.end()) {
      const base::DictValue& value = cache->second;
      if (const std::string* era = value.FindString("era")) {
        eras_[info.config.name] =
            *era == "legacy" ? mcp::Era::kLegacy : mcp::Era::kModern;
      }
      if (const std::string* version = value.FindString("version")) {
        info.version = *version;
      }
      if (const base::ListValue* tools = value.FindList("tools")) {
        for (const base::Value& tool : *tools) {
          if (std::optional<mcp::Tool> parsed =
                  tool.is_dict() ? mcp::ToolFromValue(tool.GetDict())
                                 : std::nullopt) {
            info.tools.push_back(std::move(*parsed));
          }
        }
      }
      if (const base::ListValue* prompts = value.FindList("prompts")) {
        for (const base::Value& prompt : *prompts) {
          if (std::optional<mcp::Prompt> parsed =
                  prompt.is_dict() ? mcp::PromptFromValue(prompt.GetDict())
                                   : std::nullopt) {
            info.prompts.push_back(std::move(*parsed));
          }
        }
      }
    }
    list_.push_back(std::move(info));
  }
  for (Info& info : list_) {
    if (info.on) {
      Launch(info);
    }
  }
  Changed();
}

const Connectors::Info* Connectors::Find(std::string_view name) const {
  auto found = std::ranges::find(list_, name, [](const Info& info) {
    return std::string_view(info.config.name);
  });
  return found == list_.end() ? nullptr : &*found;
}

Connectors::Info* Connectors::FindInfo(std::string_view name) {
  return const_cast<Info*>(std::as_const(*this).Find(name));
}

base::expected<void, std::string> Connectors::Add(
    std::vector<ConnectorConfig> added) {
  if (!read_) {
    return base::unexpected("uw is still reading your connectors. Try again.");
  }
  std::set<std::string> names;
  for (const ConnectorConfig& connector : added) {
    if (Find(connector.name) || !names.insert(connector.name).second) {
      return base::unexpected(
          base::StrCat({"A connector named ", connector.name,
                        " already exists. Rename it in the JSON."}));
    }
  }
  ApiKeyStore keys;
  for (ConnectorConfig& connector : added) {
    if (!connector.env.empty() || !connector.headers.empty()) {
      keys.SetKey(SecretsAccount(connector.name), SecretsToJson(connector));
    }
    for (auto& [key, value] : connector.env) {
      value.clear();
    }
    for (auto& [key, value] : connector.headers) {
      value.clear();
    }
    Info info;
    info.config = std::move(connector);
    list_.push_back(std::move(info));
    Launch(list_.back());
  }
  SaveList();
  Changed();
  return base::ok();
}

void Connectors::Remove(std::string_view name) {
  auto found = std::ranges::find(list_, name, [](const Info& info) {
    return std::string_view(info.config.name);
  });
  if (found == list_.end()) {
    return;
  }
  Stop(name);
  ApiKeyStore().DeleteKey(SecretsAccount(name));
  if (auto era = eras_.find(name); era != eras_.end()) {
    eras_.erase(era);
  }
  if (auto restarts = restarts_.find(name); restarts != restarts_.end()) {
    restarts_.erase(restarts);
  }
  files_->PostTask(FROM_HERE,
                   base::BindOnce(&DeleteFiles, folder_, std::string(name)));
  list_.erase(found);
  SaveList();
  Changed();
}

void Connectors::SetOn(std::string_view name, bool on) {
  Info* info = FindInfo(name);
  if (!info || info->on == on) {
    return;
  }
  info->on = on;
  if (on) {
    if (auto restarts = restarts_.find(name); restarts != restarts_.end()) {
      restarts_.erase(restarts);
    }
    Launch(*info);
  } else {
    Stop(name);
    info->state = State::kOff;
    info->error.clear();
  }
  SaveList();
  Changed();
}

void Connectors::SetToolOn(std::string_view name,
                           std::string_view tool,
                           bool on) {
  Info* info = FindInfo(name);
  if (!info) {
    return;
  }
  if (on) {
    info->tools_off.erase(std::string(tool));
  } else {
    info->tools_off.insert(std::string(tool));
  }
  SaveList();
  Changed();
}

void Connectors::SetAlwaysAllowed(std::string_view name,
                                  std::string_view tool,
                                  bool allowed) {
  Info* info = FindInfo(name);
  if (!info) {
    return;
  }
  if (allowed) {
    info->always_allowed.insert(std::string(tool));
  } else {
    info->always_allowed.erase(std::string(tool));
  }
  SaveList();
  Changed();
}

void Connectors::Restart(std::string_view name) {
  Info* info = FindInfo(name);
  if (!info || !info->on) {
    return;
  }
  if (auto restarts = restarts_.find(name); restarts != restarts_.end()) {
    restarts_.erase(restarts);
  }
  Stop(name);
  Launch(*info);
  Changed();
}

void Connectors::Launch(Info& info) {
  const std::string name = info.config.name;
  Stop(name);
  ConnectorConfig config = info.config;
  if (std::optional<std::string> secrets =
          ApiKeyStore().GetKey(SecretsAccount(name))) {
    SecretsFromJson(*secrets, config);
  }
  auto live = std::make_unique<Live>();
  live->generation = ++next_generation_;
  std::unique_ptr<McpTransport> transport;
  if (config.remote()) {
    if (!loader_) {
      Fail(info, "uw can't reach the web yet.");
      return;
    }
    transport = std::make_unique<RemoteTransport>(
        GURL(config.url), config.headers, loader_, RemoteTransport::Token());
  } else {
    transport = std::make_unique<LocalTransport>(
        config, shell_, LogFile(name),
        base::BindRepeating(&Connectors::OnNotification,
                            weak_factory_.GetWeakPtr(), name, live->generation),
        base::BindOnce(&Connectors::OnExit, weak_factory_.GetWeakPtr(), name,
                       live->generation));
  }
  live->client = std::make_unique<McpClient>(std::move(transport));
  McpClient& client = *live->client;
  const int generation = live->generation;
  live_[name] = std::move(live);
  info.state = State::kStarting;
  info.error.clear();
  std::optional<mcp::Era> era;
  if (auto known = eras_.find(name); known != eras_.end()) {
    era = known->second;
  }
  client.Connect(
      era, base::BindOnce(&Connectors::OnConnected, weak_factory_.GetWeakPtr(),
                          name, generation));
}

void Connectors::Stop(std::string_view name) {
  auto live = live_.find(name);
  if (live == live_.end()) {
    return;
  }
  std::vector<base::OnceClosure> waiting = std::move(live->second->waiting);
  live_.erase(live);
  for (base::OnceClosure& run : waiting) {
    std::move(run).Run();
  }
}

void Connectors::OnConnected(
    std::string name,
    int generation,
    base::expected<McpClient::Listing, McpReply> listing) {
  auto live = live_.find(name);
  Info* info = FindInfo(name);
  if (live == live_.end() || live->second->generation != generation || !info) {
    return;
  }
  if (!listing.has_value()) {
    if (listing.error().needs_sign_in) {
      info->state = State::kNeedsSignIn;
      info->error.clear();
      Stop(name);
      Changed();
      return;
    }
    Fail(*info, listing.error().problem);
    return;
  }
  eras_[name] = listing->era;
  info->version = listing->version;
  info->tools = std::move(listing->tools);
  info->prompts = std::move(listing->prompts);
  info->state = State::kReady;
  info->error.clear();
  live->second->ready = true;
  SaveCache(*info);
  std::vector<base::OnceClosure> waiting = std::move(live->second->waiting);
  for (base::OnceClosure& run : waiting) {
    std::move(run).Run();
  }
  Changed();
}

void Connectors::OnExit(std::string name,
                        int generation,
                        std::string last_error) {
  auto live = live_.find(name);
  Info* info = FindInfo(name);
  if (live == live_.end() || live->second->generation != generation || !info) {
    return;
  }
  // Only a connector that was running starts again by itself. One that
  // never got that far has its answer in OnConnected.
  if (!live->second->ready) {
    return;
  }
  if (restarts_[name]++ < kRestarts) {
    Launch(*info);
    Changed();
    return;
  }
  Fail(*info, base::StrCat({"Stopped twice: ", last_error}));
}

void Connectors::OnNotification(std::string name,
                                int generation,
                                const mcp::Message& message) {
  if (message.method != "notifications/tools/list_changed" &&
      message.method != "notifications/prompts/list_changed") {
    return;
  }
  auto live = live_.find(name);
  if (live == live_.end() || live->second->generation != generation ||
      !live->second->ready) {
    return;
  }
  live->second->client->List(base::BindOnce(
      &Connectors::OnListed, weak_factory_.GetWeakPtr(), name, generation));
}

void Connectors::OnListed(
    std::string name,
    int generation,
    base::expected<McpClient::Listing, McpReply> listing) {
  auto live = live_.find(name);
  Info* info = FindInfo(name);
  if (live == live_.end() || live->second->generation != generation || !info ||
      !listing.has_value()) {
    return;
  }
  info->tools = std::move(listing->tools);
  info->prompts = std::move(listing->prompts);
  SaveCache(*info);
  Changed();
}

void Connectors::Fail(Info& info, std::string error) {
  info.state = State::kFailed;
  info.error = error.empty() ? "It stopped." : std::move(error);
  Stop(info.config.name);
  Changed();
}

void Connectors::CallTool(std::string_view name,
                          std::string_view tool,
                          base::DictValue arguments,
                          Done done) {
  Request(name, "tools/call",
          base::DictValue()
              .Set("name", tool)
              .Set("arguments", std::move(arguments)),
          std::string(tool), std::move(done));
}

void Connectors::GetPrompt(std::string_view name,
                           std::string_view prompt,
                           base::DictValue arguments,
                           Done done) {
  Request(name, "prompts/get",
          base::DictValue()
              .Set("name", prompt)
              .Set("arguments", std::move(arguments)),
          std::string(prompt), std::move(done));
}

void Connectors::Request(std::string_view name,
                         std::string method,
                         base::DictValue params,
                         std::string item,
                         Done done) {
  const Info* info = Find(name);
  if (!info) {
    std::move(done).Run(base::unexpected(
        base::StrCat({"There's no connector named ", name, "."})));
    return;
  }
  if (!info->on) {
    std::move(done).Run(
        base::unexpected(base::StrCat({name, " is turned off."})));
    return;
  }
  auto live = live_.find(name);
  if (live != live_.end() && live->second->ready) {
    SendRequest(*live->second, method, std::move(params), item,
                std::move(done));
    return;
  }
  if (live != live_.end()) {
    live->second->waiting.push_back(
        base::BindOnce(&Connectors::Request, weak_factory_.GetWeakPtr(),
                       std::string(name), std::move(method), std::move(params),
                       std::move(item), std::move(done)));
    return;
  }
  std::move(done).Run(base::unexpected(
      info->state == State::kNeedsSignIn
          ? base::StrCat({"Sign in to ", name, " on uw://ai/connectors first."})
          : base::StrCat({name, " isn't running: ", info->error})));
}

void Connectors::SendRequest(Live& live,
                             const std::string& method,
                             base::DictValue params,
                             const std::string& item,
                             Done done) {
  const bool prompt = method == "prompts/get";
  live.client->Request(
      method, std::move(params), item, kCallTimeout,
      base::BindOnce(
          [](bool prompt, Done done, McpReply reply) {
            if (!reply.message) {
              std::move(done).Run(base::unexpected(reply.problem));
              return;
            }
            if (reply.message->error) {
              std::move(done).Run(base::unexpected(base::StrCat(
                  {"The connector refused: ", reply.message->error->message})));
              return;
            }
            std::move(done).Run(
                prompt ? mcp::ParsePromptResult(reply.message->result)
                       : mcp::ParseToolResult(reply.message->result));
          },
          prompt, std::move(done)));
}

std::string Connectors::CommandLine(std::string_view name) const {
  const Info* info = Find(name);
  if (!info || info->config.remote()) {
    return std::string();
  }
  std::vector<base::FilePath> words = {base::FilePath(info->config.command)};
  for (const std::string& arg : info->config.args) {
    words.push_back(base::FilePath(arg));
  }
  return QuotePathsForShell(words);
}

std::map<std::string, std::string> Connectors::Environment(
    std::string_view name) const {
  const Info* info = Find(name);
  if (!info || info->config.remote()) {
    return {};
  }
  ConnectorConfig config = info->config;
  if (std::optional<std::string> secrets =
          ApiKeyStore().GetKey(SecretsAccount(name))) {
    SecretsFromJson(*secrets, config);
  }
  return config.env;
}

base::FilePath Connectors::LogFile(std::string_view name) const {
  return folder_.AppendASCII(base::StrCat({name, ".log"}));
}

void Connectors::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void Connectors::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void Connectors::SaveList() {
  base::ListValue connectors;
  for (const Info& info : list_) {
    base::DictValue value = ConnectorToValue(info.config);
    value.Set("on", info.on);
    value.Set("tools_off", ToList(info.tools_off));
    value.Set("always_allowed", ToList(info.always_allowed));
    connectors.Append(std::move(value));
  }
  files_->PostTask(
      FROM_HERE,
      base::BindOnce(&WriteFile, folder_.AppendASCII(kListFile),
                     base::WriteJson(base::DictValue().Set(
                                         "connectors", std::move(connectors)))
                         .value_or("")));
}

void Connectors::SaveCache(const Info& info) {
  base::ListValue tools;
  for (const mcp::Tool& tool : info.tools) {
    tools.Append(mcp::ToolToValue(tool));
  }
  base::ListValue prompts;
  for (const mcp::Prompt& prompt : info.prompts) {
    prompts.Append(mcp::PromptToValue(prompt));
  }
  auto era = eras_.find(info.config.name);
  files_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &WriteFile, folder_.AppendASCII(info.config.name + ".json"),
          base::WriteJson(
              base::DictValue()
                  .Set("era",
                       era != eras_.end() && era->second == mcp::Era::kLegacy
                           ? "legacy"
                           : "modern")
                  .Set("version", info.version)
                  .Set("tools", std::move(tools))
                  .Set("prompts", std::move(prompts)))
              .value_or("")));
}

void Connectors::Changed() {
  for (Observer& observer : observers_) {
    observer.OnConnectorsChanged();
  }
}

}  // namespace uw
