// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_CONNECTORS_CONNECTOR_CATALOG_STORE_H_
#define UW_BROWSER_CONNECTORS_CONNECTOR_CATALOG_STORE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "url/gurl.h"
#include "uw/components/connectors/catalog.h"

namespace base {
class SequencedTaskRunner;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// uw's copy of the MCP Registry's web servers and of Claude's and OpenAI's
// plugin directories, which the connect dialog searches. It lives in the
// user data folder and is refreshed in the background when the dialog
// opens and the copy is a day old. A refresh that stops partway through the
// registry carries on from there the next time. See
// docs/plans/connect-dialog.md.
class ConnectorCatalogStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnCatalogChanged() = 0;
  };

  static ConnectorCatalogStore& Get();

  ConnectorCatalogStore(const ConnectorCatalogStore&) = delete;
  ConnectorCatalogStore& operator=(const ConnectorCatalogStore&) = delete;

  // Reads the copy the first time, and refreshes it when it is due.
  void Open();
  const ConnectorCatalog& catalog() const { return catalog_; }
  // The saved copy has been read.
  bool loaded() const { return read_; }
  bool refreshing() const { return refreshing_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Keeps the copy in `folder` and reads the registry at `registry` and
  // GitHub's files at `raw`, with `loader`. Forgets the copy.
  static void SetUpForTesting(
      const base::FilePath& folder,
      const GURL& registry,
      const GURL& raw,
      scoped_refptr<network::SharedURLLoaderFactory> loader);

 private:
  friend class base::NoDestructor<ConnectorCatalogStore>;
  using Body = std::optional<std::string>;

  ConnectorCatalogStore();
  ~ConnectorCatalogStore();

  void OnRead(std::optional<ConnectorCatalog> saved);
  void Refresh();
  void FetchRegistry();
  void OnRegistryPage(Body body);
  void FetchDirectories();
  void OnClaudeDirectory(Body body);
  void OnOpenAiDirectory(Body body);
  void ReadNextPlugins();
  void OnPluginFile(PluginRef plugin, std::string mcp, Body body);
  void PluginDone(std::optional<CatalogEntry> entry, std::string folder);
  void Finish();
  void Download(const GURL& url, base::OnceCallback<void(Body)> done);
  void OnDownloaded(int id, base::OnceCallback<void(Body)> done, Body body);
  void Save();
  void Changed();

  base::FilePath file_;
  GURL registry_;
  GURL raw_;
  scoped_refptr<network::SharedURLLoaderFactory> loader_;
  scoped_refptr<base::SequencedTaskRunner> files_;
  ConnectorCatalog catalog_;
  bool reading_ = false;
  bool read_ = false;
  bool refreshing_ = false;
  // What this refresh has read so far.
  bool registry_done_ = false;
  bool directories_done_ = false;
  std::vector<PluginRef> claude_;
  std::vector<PluginRef> waiting_plugins_;
  size_t plugins_running_ = 0;
  std::vector<std::pair<std::string, CatalogEntry>> plugins_read_;
  std::map<int, std::unique_ptr<network::SimpleURLLoader>> downloads_;
  int next_download_ = 0;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<ConnectorCatalogStore> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_CONNECTOR_CATALOG_STORE_H_
