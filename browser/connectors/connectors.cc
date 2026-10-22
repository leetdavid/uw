// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/connectors.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/common/chrome_paths.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/connectors/local_transport.h"
#include "uw/browser/connectors/remote_transport.h"
#include "uw/components/connectors/shell_environment.h"
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
// How long the user's shell may take to read their startup files.
constexpr base::TimeDelta kEnvironmentLimit = base::Seconds(10);
// How much of a failed connector's log its page shows.
constexpr size_t kLogLines = 8;

std::string SecretsAccount(std::string_view name) {
  return base::StrCat({"connector:", name});
}

std::string TokensAccount(std::string_view name) {
  return base::StrCat({"connector-oauth:", name});
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

// The environment the user's terminal has: what `shell` holds as an
// interactive login shell, once it has read their startup files. Nothing
// when it fails or takes longer than kEnvironmentLimit. Blocks.
std::optional<base::EnvironmentMap> ReadShellEnvironment(base::FilePath shell) {
  std::array<int, 2> output;
  if (pipe(output.data()) != 0) {
    return std::nullopt;
  }
  base::ScopedFD read_end(output[0]);
  base::ScopedFD write_end(output[1]);
  base::ScopedFD nothing(HANDLE_EINTR(open("/dev/null", O_RDWR)));
  if (!nothing.is_valid()) {
    return std::nullopt;
  }
  base::LaunchOptions options;
  options.fds_to_remap = {{nothing.get(), STDIN_FILENO},
                          {write_end.get(), STDOUT_FILENO},
                          {nothing.get(), STDERR_FILENO}};
  options.current_directory = base::GetHomeDir();
  options.new_process_group = true;
  base::Process process = base::LaunchProcess(
      {shell.value(), "-i", "-l", "-c", ShellEnvironmentCommand()}, options);
  write_end.reset();
  if (!process.IsValid()) {
    return std::nullopt;
  }
  // A program the startup files leave running can keep the output open, so
  // the reading stops at the closing mark rather than at the end.
  const base::TimeTicks deadline = base::TimeTicks::Now() + kEnvironmentLimit;
  std::optional<base::EnvironmentMap> environment;
  std::string printed;
  std::array<char, 16 * 1024> buffer;
  while (!environment) {
    const base::TimeDelta left = deadline - base::TimeTicks::Now();
    pollfd ready = {read_end.get(), POLLIN, 0};
    if (!left.is_positive() ||
        HANDLE_EINTR(
            poll(&ready, 1, static_cast<int>(left.InMilliseconds()))) <= 0) {
      kill(-process.Pid(), SIGKILL);
      break;
    }
    const ssize_t got =
        HANDLE_EINTR(read(read_end.get(), buffer.data(), buffer.size()));
    if (got <= 0) {
      break;
    }
    printed.append(buffer.data(), static_cast<size_t>(got));
    environment = ParseShellEnvironment(printed);
  }
  base::EnsureProcessTerminated(std::move(process));
  return environment;
}

// The last lines of a connector's log.
std::vector<std::string> LogTail(base::FilePath log) {
  std::string text;
  if (!base::ReadFileToString(log, &text)) {
    return {};
  }
  std::vector<std::string> lines = base::SplitString(
      text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (lines.size() > kLogLines) {
    lines.erase(lines.begin(), lines.end() - kLogLines);
  }
  return lines;
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
  connectors.sign_ins_.clear();
  connectors.sign_in_hints_.clear();
  connectors.tokens_.clear();
  connectors.refreshed_.clear();
  connectors.started_ = false;
  connectors.read_ = false;
  connectors.environment_.reset();
  connectors.environment_problem_.clear();
  connectors.folder_ = folder;
  connectors.shell_ = shell;
  connectors.loader_ = nullptr;
}

void Connectors::Start() {
  if (started_) {
    return;
  }
  started_ = true;
  // Connectors serve every profile, so they use the browser's own network
  // context, without any profile's cookies.
  loader_ = g_browser_process->shared_url_loader_factory();
  if (shell_.empty()) {
    shell_ = UserShell();
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadShellEnvironment, shell_),
      base::BindOnce(&Connectors::OnEnvironment, weak_factory_.GetWeakPtr()));
}

void Connectors::OnEnvironment(
    std::optional<base::EnvironmentMap> environment) {
  environment_ = std::move(environment);
  if (!environment_) {
    environment_problem_ =
        "uw couldn't read your terminal's environment, so programs on this "
        "Mac start through your login shell and may miss what your shell's "
        "startup files set.";
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
  ApiKeyStore keys;
  bool converted = false;
  for (const base::Value& item : *connectors) {
    const base::DictValue* dict = item.GetIfDict();
    std::optional<ConnectorConfig> config =
        dict ? ConnectorFromValue(*dict) : std::nullopt;
    if (!config || FindInfo(config->name)) {
      continue;
    }
    // One saved as mcp-remote becomes the web connector it passes on to.
    ConnectorConfig with_secrets = *config;
    if (std::optional<std::string> secrets =
            keys.GetKey(SecretsAccount(config->name))) {
      SecretsFromJson(*secrets, with_secrets);
    }
    if (std::optional<ConnectorConfig> web = FromMcpRemote(with_secrets)) {
      if (std::optional<std::string> address = StreamableAddress(web->url)) {
        web->url = *address;
      }
      if (web->headers.empty()) {
        keys.DeleteKey(SecretsAccount(web->name));
      } else {
        keys.SetKey(SecretsAccount(web->name), SecretsToJson(*web));
      }
      for (auto& [key, value] : web->headers) {
        value.clear();
      }
      caches.erase(web->name);
      config = std::move(*web);
      converted = true;
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
  if (converted) {
    SaveList();
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
  ForgetTokens(name);
  if (auto sign_in = sign_ins_.find(name); sign_in != sign_ins_.end()) {
    sign_ins_.erase(sign_in);
  }
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

void Connectors::Probe(const GURL& url, base::OnceCallback<void(bool)> done) {
  if (!loader_) {
    std::move(done).Run(false);
    return;
  }
  auto client = std::make_unique<McpClient>(std::make_unique<RemoteTransport>(
      url, std::map<std::string, std::string>(), loader_,
      base::BindRepeating([] { return std::string(); })));
  McpClient& connect = *client;
  connect.Connect(
      std::nullopt,
      base::BindOnce(
          [](std::unique_ptr<McpClient> client,
             base::OnceCallback<void(bool)> done,
             base::expected<McpClient::Listing, McpReply> listing) {
            base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
                FROM_HERE, std::move(client));
            std::move(done).Run(listing.has_value() ||
                                listing.error().needs_sign_in);
          },
          std::move(client), std::move(done)));
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
    if (!tokens_.contains(name)) {
      if (std::optional<std::string> saved =
              ApiKeyStore().GetKey(TokensAccount(name))) {
        if (std::optional<oauth::Tokens> tokens =
                oauth::TokensFromJson(*saved)) {
          tokens_[name] = std::move(*tokens);
        }
      }
    }
    transport = std::make_unique<RemoteTransport>(
        GURL(config.url), config.headers, loader_,
        base::BindRepeating(
            [](std::string name) { return Get().AccessToken(name); }, name));
  } else {
    transport = std::make_unique<LocalTransport>(
        config, shell_, environment_, LogFile(name),
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
  info.log_lines.clear();
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
      sign_in_hints_[name] = listing.error().resource_metadata;
      Stop(name);
      const bool signed_in = tokens_.contains(name);
      Refresh(name, base::BindOnce(
                        [](std::string name, bool signed_in, bool refreshed) {
                          Connectors& connectors = Get();
                          Info* info = connectors.FindInfo(name);
                          if (!info || !info->on) {
                            return;
                          }
                          if (refreshed) {
                            connectors.Launch(*info);
                          } else {
                            connectors.ForgetTokens(name);
                            info->state = State::kNeedsSignIn;
                            info->error =
                                signed_in ? "Signed out: the sign-in expired "
                                            "and couldn't refresh."
                                          : std::string();
                          }
                          connectors.Changed();
                        },
                        name, signed_in));
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
  refreshed_.erase(name);
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
  if (!info.config.remote()) {
    files_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&LogTail, LogFile(info.config.name)),
        base::BindOnce(&Connectors::OnLogTail, weak_factory_.GetWeakPtr(),
                       info.config.name));
  }
  Changed();
}

void Connectors::OnLogTail(std::string name, std::vector<std::string> lines) {
  Info* info = FindInfo(name);
  if (info && info->state == State::kFailed) {
    info->log_lines = std::move(lines);
    Changed();
  }
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
    base::DictValue retry = params.Clone();
    live->second->client->Request(
        method, std::move(params), item, kCallTimeout,
        base::BindOnce(&Connectors::OnAnswer, weak_factory_.GetWeakPtr(),
                       std::string(name), method, std::move(retry), item,
                       std::move(done)));
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

void Connectors::OnAnswer(std::string name,
                          std::string method,
                          base::DictValue retry,
                          std::string item,
                          Done done,
                          McpReply reply) {
  if (reply.needs_sign_in) {
    Refresh(
        name,
        base::BindOnce(
            [](std::string name, std::string method, base::DictValue retry,
               std::string item, Done done, bool refreshed) {
              if (!refreshed) {
                Get().SignOut(name);
                std::move(done).Run(base::unexpected(base::StrCat(
                    {"Sign in to ", name, " again on uw://ai/connectors."})));
                return;
              }
              Get().Request(name, std::move(method), std::move(retry),
                            std::move(item), std::move(done));
            },
            name, std::move(method), std::move(retry), std::move(item),
            std::move(done)));
    return;
  }
  if (!reply.message) {
    std::move(done).Run(base::unexpected(reply.problem));
    return;
  }
  if (reply.message->error) {
    std::move(done).Run(base::unexpected(base::StrCat(
        {"The connector refused: ", reply.message->error->message})));
    return;
  }
  std::move(done).Run(method == "prompts/get"
                          ? mcp::ParsePromptResult(reply.message->result)
                          : mcp::ParseToolResult(reply.message->result));
}

void Connectors::SignIn(std::string_view name, ConnectorSignIn::OpenTab open) {
  Info* info = FindInfo(name);
  if (!info || !info->config.remote() || !loader_) {
    return;
  }
  Stop(name);
  info->state = State::kSigningIn;
  info->error.clear();
  GURL hint;
  if (auto found = sign_in_hints_.find(name); found != sign_in_hints_.end()) {
    hint = GURL(found->second);
  }
  sign_ins_[std::string(name)] = std::make_unique<ConnectorSignIn>(
      GURL(info->config.url), hint, loader_, std::move(open),
      base::BindOnce(&Connectors::OnSignedIn, weak_factory_.GetWeakPtr(),
                     std::string(name)));
  Changed();
}

void Connectors::CancelSignIn(std::string_view name) {
  auto sign_in = sign_ins_.find(name);
  Info* info = FindInfo(name);
  if (sign_in == sign_ins_.end() || !info) {
    return;
  }
  sign_ins_.erase(sign_in);
  info->state = State::kNeedsSignIn;
  info->error.clear();
  Changed();
}

void Connectors::SignOut(std::string_view name) {
  Info* info = FindInfo(name);
  if (!info || !info->config.remote()) {
    return;
  }
  ForgetTokens(name);
  Stop(name);
  info->state = State::kNeedsSignIn;
  info->error.clear();
  Changed();
}

void Connectors::OnSignedIn(std::string name,
                            base::expected<oauth::Tokens, SignInError> result) {
  // The sign-in is still in its own call; it is deleted afterwards.
  if (auto sign_in = sign_ins_.find(name); sign_in != sign_ins_.end()) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(sign_in->second));
    sign_ins_.erase(sign_in);
  }
  Info* info = FindInfo(name);
  if (!info) {
    return;
  }
  if (!result.has_value()) {
    info->state = result.error().cannot_register ? State::kCantSignIn
                                                 : State::kNeedsSignIn;
    info->error = result.error().message;
    Changed();
    return;
  }
  KeepTokens(name, *result);
  refreshed_.erase(name);
  if (info->on) {
    Launch(*info);
  }
  Changed();
}

std::string Connectors::AccessToken(std::string_view name) const {
  auto tokens = tokens_.find(name);
  return tokens == tokens_.end() ? std::string() : tokens->second.access;
}

void Connectors::Refresh(std::string_view name,
                         base::OnceCallback<void(bool)> done) {
  auto tokens = tokens_.find(name);
  const Info* info = Find(name);
  if (tokens == tokens_.end() || !info || refreshed_.contains(name) ||
      !loader_) {
    std::move(done).Run(false);
    return;
  }
  refreshed_.insert(std::string(name));
  ConnectorSignIn::Refresh(
      tokens->second, GURL(info->config.url), loader_,
      base::BindOnce(&Connectors::OnRefreshed, weak_factory_.GetWeakPtr(),
                     std::string(name), std::move(done)));
}

void Connectors::OnRefreshed(std::string name,
                             base::OnceCallback<void(bool)> done,
                             std::optional<oauth::Tokens> tokens) {
  if (tokens) {
    KeepTokens(name, *tokens);
  }
  std::move(done).Run(tokens.has_value());
}

void Connectors::KeepTokens(std::string_view name,
                            const oauth::Tokens& tokens) {
  tokens_[std::string(name)] = tokens;
  ApiKeyStore().SetKey(TokensAccount(name), oauth::TokensToJson(tokens));
}

void Connectors::ForgetTokens(std::string_view name) {
  if (auto tokens = tokens_.find(name); tokens != tokens_.end()) {
    tokens_.erase(tokens);
  }
  ApiKeyStore().DeleteKey(TokensAccount(name));
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
