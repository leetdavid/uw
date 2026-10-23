// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_CONNECTORS_CATALOG_H_
#define UW_COMPONENTS_CONNECTORS_CATALOG_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "uw/components/connectors/connector_config.h"

namespace uw {

// Where an entry of the connect dialog comes from. See
// docs/plans/connect-dialog.md.
enum class CatalogSource {
  kRecommended,
  kClaudePlugin,
  kOpenAiPlugin,
  kRegistry,
};

// Something the user types before a connector can start, such as a key.
struct CatalogField {
  // What `${name}` stands for in the entry's headers.
  std::string name;
  std::string description;
  bool required = false;
  bool secret = false;
  // What the field starts with.
  std::string value;
};

// A web connector the connect dialog offers.
struct CatalogEntry {
  CatalogEntry();
  CatalogEntry(const CatalogEntry&);
  CatalogEntry& operator=(const CatalogEntry&);
  CatalogEntry(CatalogEntry&&);
  CatalogEntry& operator=(CatalogEntry&&);
  ~CatalogEntry();

  // Unique across sources, such as "registry:app.linear/linear".
  std::string id;
  CatalogSource source = CatalogSource::kRegistry;
  // The connector's name when it is added, such as "linear".
  std::string name;
  std::string title;
  // One line.
  std::string description;
  std::string website;
  std::string url;
  // Values may hold `${field}`.
  std::map<std::string, std::string> headers;
  std::vector<CatalogField> fields;
};

// uw's own short list, each tested with a real account.
const std::vector<CatalogEntry>& RecommendedConnectors();

// One page of the MCP Registry's server list: the latest version of each web
// server, the names of those no longer active, and where the next page
// starts. Nothing for a page that isn't one.
struct RegistryPage {
  RegistryPage();
  RegistryPage(RegistryPage&&);
  RegistryPage& operator=(RegistryPage&&);
  ~RegistryPage();

  std::vector<CatalogEntry> entries;
  std::vector<std::string> removed;
  std::string next_cursor;
};
std::optional<RegistryPage> ParseRegistryPage(std::string_view json);

// A plugin a directory lists, and the address of its folder, ending in "/".
struct PluginRef {
  CatalogSource source = CatalogSource::kClaudePlugin;
  std::string name;
  std::string title;
  std::string description;
  std::string website;
  std::string folder;
};
// Claude's marketplace.json and OpenAI's. `raw` is where GitHub serves a
// repository's files, such as "https://raw.githubusercontent.com/". Plugins
// outside GitHub are left out.
std::vector<PluginRef> ParseClaudeMarketplace(std::string_view json,
                                              std::string_view raw);
std::vector<PluginRef> ParseOpenAiMarketplace(std::string_view json,
                                              std::string_view raw);
// Whether `folder` names a fixed commit, so its files never change.
bool IsPinned(std::string_view folder);

// The plugin's web connector, from its .mcp.json, or from the servers its
// manifest lists. The manifest also gives OpenAI's plugins their title and
// description. Nothing when it has no web connector.
std::optional<CatalogEntry> PluginEntry(const PluginRef& plugin,
                                        std::string_view mcp_json,
                                        std::string_view manifest_json);

// The connector `entry` makes under `name`, with the fields' `values` put in
// its headers. The error names a required field left empty.
base::expected<ConnectorConfig, std::string> ConnectorFor(
    const CatalogEntry& entry,
    std::string_view name,
    const std::map<std::string, std::string>& values);

// Whether two addresses are the same server: the same host and path, apart
// from a query and a closing "/".
bool SameServer(std::string_view a, std::string_view b);

// uw's copy of the registry and both directories.
class ConnectorCatalog {
 public:
  ConnectorCatalog();
  ConnectorCatalog(const ConnectorCatalog&);
  ConnectorCatalog& operator=(const ConnectorCatalog&);
  ConnectorCatalog(ConnectorCatalog&&);
  ConnectorCatalog& operator=(ConnectorCatalog&&);
  ~ConnectorCatalog();

  static std::optional<ConnectorCatalog> FromJson(std::string_view json);
  std::string ToJson() const;

  // Takes a page of the registry in. Entries replace those of the same id.
  void AddRegistryPage(RegistryPage page);
  // The plugins with web connectors, replacing those uw had, with the folder
  // each was read from.
  void SetPlugins(std::vector<std::pair<std::string, CatalogEntry>> plugins);
  // A plugin read before from `folder`, when its folder is pinned.
  const CatalogEntry* PinnedPlugin(std::string_view folder) const;

  // Up to `limit` entries for `query`, one per server: Recommended first,
  // then Claude's plugins, OpenAI's, and the registry. An empty query gives
  // Recommended.
  std::vector<CatalogEntry> Search(std::string_view query, size_t limit) const;
  // Recommended included.
  std::optional<CatalogEntry> Find(std::string_view id) const;

  size_t registry_size() const { return registry_.size(); }

  // When a whole refresh last ended.
  base::Time refreshed;
  // The registry is asked only for what changed since this.
  base::Time registry_since;
  // Where an unfinished pass over the registry goes on, and when it began.
  std::string registry_cursor;
  base::Time registry_pass;

 private:
  std::map<std::string, CatalogEntry, std::less<>> registry_;
  std::vector<std::pair<std::string, CatalogEntry>> plugins_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_CONNECTORS_CATALOG_H_
