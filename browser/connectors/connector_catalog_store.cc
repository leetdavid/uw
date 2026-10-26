// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/connectors/connector_catalog_store.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/common/chrome_paths.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace uw {
namespace {

constexpr char kFileName[] = "catalog.json";
constexpr base::TimeDelta kRefreshAfter = base::Days(1);
constexpr base::TimeDelta kTimeout = base::Seconds(60);
// A page of the registry was about 90 kB on 2026-10-22.
constexpr size_t kMaxBytes = 4 * 1024 * 1024;
// How many plugins are read at once.
constexpr size_t kPluginsAtOnce = 6;

constexpr char kClaudeDirectory[] =
    "anthropics/claude-plugins-official/main/.claude-plugin/marketplace.json";
constexpr char kOpenAiDirectory[] =
    "openai/plugins/main/.agents/plugins/marketplace.json";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_connector_catalog", R"(
      semantics {
        sender: "uw connectors"
        description:
          "Downloads the public MCP Registry's list of web servers, and "
          "Claude's and OpenAI's public plugin directories with each "
          "plugin's connector file, so the connect dialog can search them."
        trigger:
          "The user opens the connect dialog and uw's copy is a day old, or "
          "the last refresh stopped partway."
        data: "None. The requests carry no cookies and nothing about the user."
        destination: OTHER
        destination_other:
          "registry.modelcontextprotocol.io and raw.githubusercontent.com"
      }
      policy {
        cookies_allowed: NO
        setting: "Only the connect dialog asks for it."
        policy_exception_justification: "Not implemented."
      })");

// `time` as the registry takes it, such as 2026-10-22T09:12:00Z.
std::string Rfc3339(base::Time time) {
  base::Time::Exploded utc;
  time.UTCExplode(&utc);
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ", utc.year,
                            utc.month, utc.day_of_month, utc.hour, utc.minute,
                            utc.second);
}

std::optional<ConnectorCatalog> ReadSaved(base::FilePath file) {
  std::string json;
  if (!base::ReadFileToString(file, &json)) {
    return std::nullopt;
  }
  return ConnectorCatalog::FromJson(json);
}

void WriteSaved(base::FilePath file, std::string json) {
  base::CreateDirectory(file.DirName());
  base::ImportantFileWriter::WriteFileAtomically(file, json);
}

}  // namespace

// static
ConnectorCatalogStore& ConnectorCatalogStore::Get() {
  static base::NoDestructor<ConnectorCatalogStore> store;
  return *store;
}

ConnectorCatalogStore::ConnectorCatalogStore()
    : registry_("https://registry.modelcontextprotocol.io/"),
      raw_("https://raw.githubusercontent.com/"),
      files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  base::FilePath data;
  base::PathService::Get(chrome::DIR_USER_DATA, &data);
  file_ = data.AppendASCII("connectors").AppendASCII(kFileName);
}

ConnectorCatalogStore::~ConnectorCatalogStore() = default;

// static
void ConnectorCatalogStore::SetUpForTesting(
    const base::FilePath& folder,
    const GURL& registry,
    const GURL& raw,
    scoped_refptr<network::SharedURLLoaderFactory> loader) {
  ConnectorCatalogStore& store = Get();
  store.weak_factory_.InvalidateWeakPtrs();
  store.downloads_.clear();
  store.catalog_ = ConnectorCatalog();
  store.reading_ = store.read_ = store.refreshing_ = false;
  store.claude_.clear();
  store.waiting_plugins_.clear();
  store.plugins_read_.clear();
  store.plugins_running_ = 0;
  store.file_ = folder.AppendASCII(kFileName);
  store.registry_ = registry;
  store.raw_ = raw;
  store.loader_ = std::move(loader);
}

void ConnectorCatalogStore::Open() {
  if (!read_) {
    if (!reading_) {
      reading_ = true;
      files_->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&ReadSaved, file_),
          base::BindOnce(&ConnectorCatalogStore::OnRead,
                         weak_factory_.GetWeakPtr()));
    }
    return;
  }
  Refresh();
}

void ConnectorCatalogStore::OnRead(std::optional<ConnectorCatalog> saved) {
  reading_ = false;
  read_ = true;
  if (saved) {
    catalog_ = std::move(*saved);
  }
  Changed();
  Refresh();
}

// Does nothing while a refresh runs, or when the copy is less than a day
// old and whole.
void ConnectorCatalogStore::Refresh() {
  if (refreshing_ || (catalog_.registry_cursor.empty() &&
                      base::Time::Now() - catalog_.refreshed < kRefreshAfter)) {
    return;
  }
  if (!loader_) {
    loader_ = g_browser_process->shared_url_loader_factory();
  }
  refreshing_ = true;
  registry_done_ = false;
  directories_done_ = false;
  if (catalog_.registry_cursor.empty()) {
    catalog_.registry_pass = base::Time::Now();
  }
  Changed();
  FetchRegistry();
}

void ConnectorCatalogStore::FetchRegistry() {
  GURL url = net::AppendQueryParameter(registry_.Resolve("v0/servers"),
                                       "limit", "100");
  url = net::AppendQueryParameter(url, "version", "latest");
  if (!catalog_.registry_since.is_null()) {
    url = net::AppendQueryParameter(url, "updated_since",
                                    Rfc3339(catalog_.registry_since));
  }
  if (!catalog_.registry_cursor.empty()) {
    url = net::AppendQueryParameter(url, "cursor", catalog_.registry_cursor);
  }
  Download(url, base::BindOnce(&ConnectorCatalogStore::OnRegistryPage,
                               weak_factory_.GetWeakPtr()));
}

void ConnectorCatalogStore::OnRegistryPage(Body body) {
  std::optional<RegistryPage> page =
      body ? ParseRegistryPage(*body) : std::nullopt;
  if (!page) {
    // The pass carries on from this page next time.
    FetchDirectories();
    return;
  }
  const bool last = page->next_cursor.empty() ||
                    (page->entries.empty() && page->removed.empty());
  catalog_.registry_cursor = last ? std::string() : page->next_cursor;
  catalog_.AddRegistryPage(std::move(*page));
  if (last) {
    catalog_.registry_since = catalog_.registry_pass;
    registry_done_ = true;
  }
  Save();
  Changed();
  if (last) {
    FetchDirectories();
  } else {
    FetchRegistry();
  }
}

void ConnectorCatalogStore::FetchDirectories() {
  Download(raw_.Resolve(kClaudeDirectory),
           base::BindOnce(&ConnectorCatalogStore::OnClaudeDirectory,
                          weak_factory_.GetWeakPtr()));
}

void ConnectorCatalogStore::OnClaudeDirectory(Body body) {
  if (!body) {
    Finish();
    return;
  }
  claude_ = ParseClaudeMarketplace(*body, raw_.spec());
  Download(raw_.Resolve(kOpenAiDirectory),
           base::BindOnce(&ConnectorCatalogStore::OnOpenAiDirectory,
                          weak_factory_.GetWeakPtr()));
}

void ConnectorCatalogStore::OnOpenAiDirectory(Body body) {
  if (!body) {
    Finish();
    return;
  }
  waiting_plugins_ = std::move(claude_);
  for (PluginRef& plugin : ParseOpenAiMarketplace(*body, raw_.spec())) {
    waiting_plugins_.push_back(std::move(plugin));
  }
  plugins_read_.clear();
  ReadNextPlugins();
}

void ConnectorCatalogStore::ReadNextPlugins() {
  while (plugins_running_ < kPluginsAtOnce && !waiting_plugins_.empty()) {
    PluginRef plugin = std::move(waiting_plugins_.back());
    waiting_plugins_.pop_back();
    if (const CatalogEntry* known = catalog_.PinnedPlugin(plugin.folder)) {
      plugins_read_.emplace_back(plugin.folder, *known);
      continue;
    }
    ++plugins_running_;
    const GURL mcp = GURL(plugin.folder).Resolve(".mcp.json");
    Download(mcp, base::BindOnce(
                      [](base::WeakPtr<ConnectorCatalogStore> self,
                         PluginRef plugin, Body body) {
                        if (self) {
                          self->OnPluginFile(std::move(plugin),
                                             body.value_or(std::string()),
                                             std::nullopt);
                        }
                      },
                      weak_factory_.GetWeakPtr(), plugin));
  }
  if (plugins_running_ == 0 && waiting_plugins_.empty()) {
    catalog_.SetPlugins(std::move(plugins_read_));
    plugins_read_.clear();
    directories_done_ = true;
    Finish();
  }
}

// Called with the plugin's .mcp.json, then again with its manifest when uw
// needs that too: an OpenAI plugin's describes it, and a Claude plugin's
// can list its servers.
void ConnectorCatalogStore::OnPluginFile(PluginRef plugin,
                                         std::string mcp,
                                         Body manifest) {
  if (!manifest) {
    std::optional<CatalogEntry> entry = PluginEntry(plugin, mcp, "");
    const bool openai = plugin.source == CatalogSource::kOpenAiPlugin;
    if (!openai && entry) {
      PluginDone(std::move(entry), plugin.folder);
      return;
    }
    const GURL file = GURL(plugin.folder).Resolve(
        openai ? ".codex-plugin/plugin.json" : ".claude-plugin/plugin.json");
    Download(file, base::BindOnce(
                       [](base::WeakPtr<ConnectorCatalogStore> self,
                          PluginRef plugin, std::string mcp, Body body) {
                         if (self) {
                           self->OnPluginFile(std::move(plugin), std::move(mcp),
                                              body.value_or(std::string()));
                         }
                       },
                       weak_factory_.GetWeakPtr(), plugin, std::move(mcp)));
    return;
  }
  PluginDone(PluginEntry(plugin, mcp, *manifest), plugin.folder);
}

void ConnectorCatalogStore::PluginDone(std::optional<CatalogEntry> entry,
                                       std::string folder) {
  --plugins_running_;
  if (entry) {
    plugins_read_.emplace_back(std::move(folder), std::move(*entry));
  }
  ReadNextPlugins();
}

// Ends the refresh. It counts as a whole one when the registry's pass ended
// and both directories were read.
void ConnectorCatalogStore::Finish() {
  if (registry_done_ && directories_done_) {
    catalog_.refreshed = base::Time::Now();
  }
  claude_.clear();
  refreshing_ = false;
  Save();
  Changed();
}

void ConnectorCatalogStore::Download(const GURL& url,
                                     base::OnceCallback<void(Body)> done) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kTimeout);
  network::SimpleURLLoader* started = loader.get();
  const int id = next_download_++;
  downloads_[id] = std::move(loader);
  started->DownloadToString(
      loader_.get(),
      base::BindOnce(&ConnectorCatalogStore::OnDownloaded,
                     weak_factory_.GetWeakPtr(), id, std::move(done)),
      kMaxBytes);
}

void ConnectorCatalogStore::OnDownloaded(int id,
                                         base::OnceCallback<void(Body)> done,
                                         Body body) {
  downloads_.erase(id);
  std::move(done).Run(std::move(body));
}

void ConnectorCatalogStore::Save() {
  files_->PostTask(FROM_HERE,
                   base::BindOnce(&WriteSaved, file_, catalog_.ToJson()));
}

void ConnectorCatalogStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ConnectorCatalogStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ConnectorCatalogStore::Changed() {
  for (Observer& observer : observers_) {
    observer.OnCatalogChanged();
  }
}

}  // namespace uw
