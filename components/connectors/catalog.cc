// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/catalog.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace uw {
namespace {

constexpr char kOfficialMeta[] = "io.modelcontextprotocol.registry/official";
// The longest description the dialog keeps.
constexpr size_t kMaxDescription = 200;

const char* SourceName(CatalogSource source) {
  switch (source) {
    case CatalogSource::kRecommended:
      return "recommended";
    case CatalogSource::kClaudePlugin:
      return "claude";
    case CatalogSource::kOpenAiPlugin:
      return "openai";
    case CatalogSource::kRegistry:
      return "registry";
  }
}

std::optional<CatalogSource> SourceFrom(std::string_view name) {
  for (CatalogSource source :
       {CatalogSource::kRecommended, CatalogSource::kClaudePlugin,
        CatalogSource::kOpenAiPlugin, CatalogSource::kRegistry}) {
    if (name == SourceName(source)) {
      return source;
    }
  }
  return std::nullopt;
}

const base::ListValue& ListOf(const base::ListValue* list) {
  static const base::NoDestructor<base::ListValue> empty;
  return list ? *list : *empty;
}

std::string Text(const base::DictValue& dict, std::string_view key) {
  const std::string* text = dict.FindString(key);
  return text ? *text : std::string();
}

// The first sentence of the first line, cut to kMaxDescription.
std::string OneLine(std::string_view text) {
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  text = text.substr(0, text.find('\n'));
  if (const size_t stop = text.find(". "); stop != std::string_view::npos) {
    text = text.substr(0, stop + 1);
  }
  std::string line(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (line.size() > kMaxDescription) {
    base::TruncateUTF8ToByteSize(line, kMaxDescription, &line);
    line += "…";
  }
  return line;
}

// `text` as a connector name: lowercase, with anything else a name can't
// hold as a hyphen.
std::string NameFrom(std::string_view text) {
  std::string name;
  for (char c : text) {
    const bool kept = base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-';
    if (kept) {
      name.push_back(base::ToLowerASCII(c));
    } else if (!name.empty() && name.back() != '-') {
      name.push_back('-');
    }
  }
  std::string_view trimmed = base::TrimString(name, "-", base::TRIM_ALL);
  return std::string(trimmed.substr(0, 64));
}

// The names that `{name}` marks in a registry header's value, made into
// `${name}`.
std::string Template(std::string_view value, std::vector<std::string>& names) {
  std::string out;
  while (!value.empty()) {
    const size_t open = value.find('{');
    const size_t close =
        open == std::string_view::npos ? open : value.find('}', open);
    if (close == std::string_view::npos) {
      out.append(value);
      break;
    }
    const std::string name(value.substr(open + 1, close - open - 1));
    out.append(value.substr(0, open));
    base::StrAppend(&out, {"${", name, "}"});
    names.push_back(name);
    value.remove_prefix(close + 1);
  }
  return out;
}

// The `${name}` marks in a plugin's header value.
void Variables(std::string_view value, std::vector<std::string>& names) {
  for (size_t start = value.find("${"); start != std::string_view::npos;
       start = value.find("${", start + 2)) {
    const size_t end = value.find('}', start);
    if (end == std::string_view::npos) {
      return;
    }
    names.emplace_back(value.substr(start + 2, end - start - 2));
  }
}

void AddField(CatalogEntry& entry, CatalogField field) {
  if (std::ranges::none_of(entry.fields, [&](const CatalogField& each) {
        return each.name == field.name;
      })) {
    entry.fields.push_back(std::move(field));
  }
}

std::optional<CatalogEntry> RegistryEntry(const base::DictValue& server) {
  const base::ListValue* remotes = server.FindList("remotes");
  const base::DictValue* remote = nullptr;
  for (const base::Value& each : ListOf(remotes)) {
    const base::DictValue* dict = each.GetIfDict();
    const std::string* type = dict ? dict->FindString("type") : nullptr;
    const std::string* url = dict ? dict->FindString("url") : nullptr;
    if (type && *type == "streamable-http" && url && !url->contains('{') &&
        GURL(*url).SchemeIs("https")) {
      remote = dict;
      break;
    }
  }
  const std::string name = Text(server, "name");
  if (!remote || name.empty()) {
    return std::nullopt;
  }
  CatalogEntry entry;
  entry.id = base::StrCat({"registry:", name});
  entry.source = CatalogSource::kRegistry;
  entry.name = NameFrom(name.substr(name.rfind('/') + 1));
  if (entry.name.empty()) {
    return std::nullopt;
  }
  entry.title = Text(server, "title");
  if (entry.title.empty()) {
    entry.title = name.substr(name.rfind('/') + 1);
  }
  entry.description = OneLine(Text(server, "description"));
  entry.website = Text(server, "websiteUrl");
  entry.url = Text(*remote, "url");
  const base::ListValue* headers = remote->FindList("headers");
  for (const base::Value& each : ListOf(headers)) {
    const base::DictValue* header = each.GetIfDict();
    const std::string header_name = header ? Text(*header, "name") : "";
    if (header_name.empty()) {
      continue;
    }
    const bool required = header->FindBool("isRequired").value_or(false);
    const bool secret = header->FindBool("isSecret").value_or(false);
    const std::string value = Text(*header, "value");
    std::vector<std::string> marked;
    if (value.contains('{')) {
      entry.headers[header_name] = Template(value, marked);
      for (const std::string& variable : marked) {
        AddField(entry, {.name = variable,
                         .description = Text(*header, "description"),
                         .required = required,
                         .secret = secret});
      }
      continue;
    }
    entry.headers[header_name] = base::StrCat({"${", header_name, "}"});
    std::string start = value.empty() ? Text(*header, "default") : value;
    AddField(entry, {.name = header_name,
                     .description = Text(*header, "description"),
                     .required = required,
                     .secret = secret,
                     .value = std::move(start)});
  }
  return entry;
}

// Where GitHub serves the files of a plugin `source` names, or nothing for
// one outside GitHub.
std::optional<std::string> FolderOf(const base::Value* source,
                                    std::string_view raw,
                                    std::string_view repo_of_directory) {
  if (!source) {
    return std::nullopt;
  }
  const auto folder = [&](std::string_view repo, std::string_view ref,
                          std::string_view path) {
    std::string out = base::StrCat({raw, repo, "/", ref, "/"});
    path = base::TrimString(path, "./", base::TRIM_LEADING);
    path = base::TrimString(path, "/", base::TRIM_ALL);
    if (!path.empty()) {
      base::StrAppend(&out, {path, "/"});
    }
    return out;
  };
  if (source->is_string()) {
    return folder(repo_of_directory, "main", source->GetString());
  }
  const base::DictValue* dict = source->GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string kind = Text(*dict, "source");
  std::string ref = Text(*dict, "sha");
  if (ref.empty()) {
    ref = Text(*dict, "ref");
  }
  if (ref.empty()) {
    ref = "HEAD";
  }
  if (kind == "local") {
    return folder(repo_of_directory, "main", Text(*dict, "path"));
  }
  std::string repo = Text(*dict, "repo");
  if (repo.empty()) {
    const std::string address = Text(*dict, "url");
    std::string_view url = address;
    constexpr std::string_view kGitHub = "https://github.com/";
    if (!url.starts_with(kGitHub)) {
      return std::nullopt;
    }
    url.remove_prefix(kGitHub.size());
    if (url.ends_with(".git")) {
      url.remove_suffix(4);
    }
    repo = std::string(base::TrimString(url, "/", base::TRIM_ALL));
  }
  if (std::ranges::count(repo, '/') != 1) {
    return std::nullopt;
  }
  return folder(repo, ref, Text(*dict, "path"));
}

std::vector<PluginRef> ParseMarketplace(std::string_view json,
                                        std::string_view raw,
                                        CatalogSource source,
                                        std::string_view repo) {
  std::vector<PluginRef> plugins;
  std::optional<base::DictValue> top =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* list = top ? top->FindList("plugins") : nullptr;
  for (const base::Value& each : ListOf(list)) {
    const base::DictValue* dict = each.GetIfDict();
    if (!dict) {
      continue;
    }
    std::optional<std::string> folder =
        FolderOf(dict->Find("source"), raw, repo);
    PluginRef plugin;
    plugin.source = source;
    plugin.name = Text(*dict, "name");
    if (!folder || NameFrom(plugin.name).empty()) {
      continue;
    }
    plugin.folder = std::move(*folder);
    plugin.title = Text(*dict, "displayName");
    if (const base::DictValue* face = dict->FindDict("interface")) {
      if (plugin.title.empty()) {
        plugin.title = Text(*face, "displayName");
      }
    }
    plugin.description = Text(*dict, "description");
    plugin.website = Text(*dict, "homepage");
    plugins.push_back(std::move(plugin));
  }
  return plugins;
}

// The first web server in a map of servers, as .mcp.json and a manifest's
// "mcpServers" hold them.
const base::DictValue* WebServer(const base::DictValue* servers) {
  if (!servers) {
    return nullptr;
  }
  if (const base::DictValue* inner = servers->FindDict("mcpServers")) {
    servers = inner;
  }
  for (const auto [name, value] : *servers) {
    const base::DictValue* server = value.GetIfDict();
    if (!server || !server->FindString("url") || server->Find("command")) {
      continue;
    }
    const std::string type = Text(*server, "type");
    if (type.empty() || type == "http" || type == "streamable-http") {
      return server;
    }
  }
  return nullptr;
}

std::string HostAndPath(std::string_view address) {
  const GURL url{std::string(address)};
  if (!url.is_valid()) {
    return std::string(address);
  }
  std::string_view path = url.path();
  while (path.ends_with('/')) {
    path.remove_suffix(1);
  }
  return base::StrCat({base::ToLowerASCII(url.host()), path});
}

base::DictValue EntryToValue(const CatalogEntry& entry) {
  base::DictValue headers;
  for (const auto& [name, value] : entry.headers) {
    headers.Set(name, value);
  }
  base::ListValue fields;
  for (const CatalogField& field : entry.fields) {
    fields.Append(base::DictValue()
                      .Set("name", field.name)
                      .Set("description", field.description)
                      .Set("required", field.required)
                      .Set("secret", field.secret)
                      .Set("value", field.value));
  }
  return base::DictValue()
      .Set("id", entry.id)
      .Set("source", SourceName(entry.source))
      .Set("name", entry.name)
      .Set("title", entry.title)
      .Set("description", entry.description)
      .Set("website", entry.website)
      .Set("url", entry.url)
      .Set("headers", std::move(headers))
      .Set("fields", std::move(fields));
}

std::optional<CatalogEntry> EntryFromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  std::optional<CatalogSource> source =
      dict ? SourceFrom(Text(*dict, "source")) : std::nullopt;
  if (!source) {
    return std::nullopt;
  }
  CatalogEntry entry;
  entry.id = Text(*dict, "id");
  entry.source = *source;
  entry.name = Text(*dict, "name");
  entry.title = Text(*dict, "title");
  entry.description = Text(*dict, "description");
  entry.website = Text(*dict, "website");
  entry.url = Text(*dict, "url");
  if (entry.id.empty() || entry.url.empty() || !IsConnectorName(entry.name)) {
    return std::nullopt;
  }
  if (const base::DictValue* headers = dict->FindDict("headers")) {
    for (const auto [name, header] : *headers) {
      if (header.is_string()) {
        entry.headers[name] = header.GetString();
      }
    }
  }
  const base::ListValue* fields = dict->FindList("fields");
  for (const base::Value& each : ListOf(fields)) {
    if (const base::DictValue* field = each.GetIfDict()) {
      entry.fields.push_back(
          {.name = Text(*field, "name"),
           .description = Text(*field, "description"),
           .required = field->FindBool("required").value_or(false),
           .secret = field->FindBool("secret").value_or(false),
           .value = Text(*field, "value")});
    }
  }
  return entry;
}

base::Value TimeValue(base::Time time) {
  return base::TimeToValue(time);
}

base::Time TimeFrom(const base::DictValue& dict, std::string_view key) {
  const base::Value* value = dict.Find(key);
  return value ? base::ValueToTime(*value).value_or(base::Time())
               : base::Time();
}

}  // namespace

CatalogEntry::CatalogEntry() = default;
CatalogEntry::CatalogEntry(const CatalogEntry&) = default;
CatalogEntry& CatalogEntry::operator=(const CatalogEntry&) = default;
CatalogEntry::CatalogEntry(CatalogEntry&&) = default;
CatalogEntry& CatalogEntry::operator=(CatalogEntry&&) = default;
CatalogEntry::~CatalogEntry() = default;

RegistryPage::RegistryPage() = default;
RegistryPage::RegistryPage(RegistryPage&&) = default;
RegistryPage& RegistryPage::operator=(RegistryPage&&) = default;
RegistryPage::~RegistryPage() = default;

const std::vector<CatalogEntry>& RecommendedConnectors() {
  static const base::NoDestructor<std::vector<CatalogEntry>> recommended([] {
    std::vector<CatalogEntry> list;
    const auto add = [&](std::string name, std::string title,
                         std::string description, std::string website,
                         std::string url) {
      CatalogEntry entry;
      entry.id = base::StrCat({"recommended:", name});
      entry.source = CatalogSource::kRecommended;
      entry.name = std::move(name);
      entry.title = std::move(title);
      entry.description = std::move(description);
      entry.website = std::move(website);
      entry.url = std::move(url);
      list.push_back(std::move(entry));
    };
    // Each passed the sign-in check on 2026-10-22; see
    // docs/plans/connect-dialog.md.
    add("linear", "Linear", "Issues, projects, and cycles",
        "https://linear.app", "https://mcp.linear.app/mcp");
    add("granola", "Granola", "Your meeting notes and transcripts",
        "https://www.granola.ai", "https://mcp.granola.ai/mcp");
    add("notion", "Notion", "Search, create, and update pages",
        "https://www.notion.com", "https://mcp.notion.com/mcp");
    add("posthog", "PostHog", "Analytics, flags, and experiments",
        "https://posthog.com", "https://mcp.posthog.com/mcp");
    add("vercel", "Vercel", "Deployments, builds, and logs",
        "https://vercel.com", "https://mcp.vercel.com");
    add("railway", "Railway", "Deploy services and databases",
        "https://railway.com", "https://mcp.railway.com");
    add("exa", "Exa", "Web search and page contents", "https://exa.ai",
        "https://mcp.exa.ai/mcp");
    add("tldraw", "tldraw", "Draw diagrams on a canvas", "https://tldraw.com",
        "https://tldraw-mcp-app.tldraw.workers.dev/mcp");
    return list;
  }());
  return *recommended;
}

std::optional<RegistryPage> ParseRegistryPage(std::string_view json) {
  std::optional<base::DictValue> top =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* servers = top ? top->FindList("servers") : nullptr;
  if (!servers) {
    return std::nullopt;
  }
  RegistryPage page;
  for (const base::Value& each : *servers) {
    const base::DictValue* item = each.GetIfDict();
    const base::DictValue* server = item ? item->FindDict("server") : nullptr;
    if (!server) {
      continue;
    }
    const base::DictValue* meta = item->FindDict("_meta");
    const base::DictValue* official =
        meta ? meta->FindDict(kOfficialMeta) : nullptr;
    const std::string status =
        official ? Text(*official, "status") : std::string("active");
    if (official && !official->FindBool("isLatest").value_or(true)) {
      continue;
    }
    std::optional<CatalogEntry> entry = RegistryEntry(*server);
    if (status != "active" || !entry) {
      if (!Text(*server, "name").empty()) {
        page.removed.push_back(
            base::StrCat({"registry:", Text(*server, "name")}));
      }
      continue;
    }
    page.entries.push_back(std::move(*entry));
  }
  if (const base::DictValue* metadata = top->FindDict("metadata")) {
    page.next_cursor = Text(*metadata, "nextCursor");
  }
  return page;
}

std::vector<PluginRef> ParseClaudeMarketplace(std::string_view json,
                                              std::string_view raw) {
  return ParseMarketplace(json, raw, CatalogSource::kClaudePlugin,
                          "anthropics/claude-plugins-official");
}

std::vector<PluginRef> ParseOpenAiMarketplace(std::string_view json,
                                              std::string_view raw) {
  return ParseMarketplace(json, raw, CatalogSource::kOpenAiPlugin,
                          "openai/plugins");
}

bool IsPinned(std::string_view folder) {
  for (std::string_view part : base::SplitStringPiece(
           folder, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (part.size() == 40 && std::ranges::all_of(part, [](char c) {
          return base::IsHexDigit(c) && !base::IsAsciiUpper(c);
        })) {
      return true;
    }
  }
  return false;
}

std::optional<CatalogEntry> PluginEntry(const PluginRef& plugin,
                                        std::string_view mcp_json,
                                        std::string_view manifest_json) {
  std::optional<base::DictValue> mcp =
      base::JSONReader::ReadDict(mcp_json, base::JSON_PARSE_RFC);
  std::optional<base::DictValue> manifest =
      base::JSONReader::ReadDict(manifest_json, base::JSON_PARSE_RFC);
  const base::DictValue* server = WebServer(mcp ? &*mcp : nullptr);
  if (!server && manifest) {
    server = WebServer(manifest->FindDict("mcpServers"));
  }
  if (!server) {
    return std::nullopt;
  }
  CatalogEntry entry;
  entry.source = plugin.source;
  entry.id = base::StrCat({SourceName(plugin.source), ":", plugin.name});
  entry.name = NameFrom(plugin.name);
  entry.url = Text(*server, "url");
  if (entry.url.contains("${") || !GURL(entry.url).SchemeIs("https")) {
    return std::nullopt;
  }
  entry.title = plugin.title;
  std::string description = plugin.description;
  entry.website = plugin.website;
  if (manifest) {
    if (const base::DictValue* face = manifest->FindDict("interface")) {
      if (entry.title.empty()) {
        entry.title = Text(*face, "displayName");
      }
      if (description.empty()) {
        description = Text(*face, "shortDescription");
      }
    }
    if (description.empty()) {
      description = Text(*manifest, "description");
    }
    if (entry.website.empty()) {
      entry.website = Text(*manifest, "homepage");
    }
  }
  if (entry.title.empty()) {
    entry.title = plugin.name;
  }
  entry.description = OneLine(description);
  if (const base::DictValue* headers = server->FindDict("headers")) {
    for (const auto [name, value] : *headers) {
      if (!value.is_string()) {
        continue;
      }
      entry.headers[name] = value.GetString();
      std::vector<std::string> variables;
      Variables(value.GetString(), variables);
      for (std::string& variable : variables) {
        AddField(entry, {.name = std::move(variable),
                         .required = true,
                         .secret = true});
      }
    }
  }
  return entry;
}

base::expected<ConnectorConfig, std::string> ConnectorFor(
    const CatalogEntry& entry,
    std::string_view name,
    const std::map<std::string, std::string>& values) {
  std::map<std::string, std::string> filled;
  for (const CatalogField& field : entry.fields) {
    auto given = values.find(field.name);
    std::string value(base::TrimWhitespaceASCII(
        given == values.end() ? field.value : given->second, base::TRIM_ALL));
    if (value.empty() && field.required) {
      return base::unexpected(base::StrCat(
          {"Fill in ", field.description.empty() ? field.name
                                                 : field.description,
           "."}));
    }
    filled[field.name] = std::move(value);
  }
  ConnectorConfig config;
  config.name = std::string(name);
  config.url = entry.url;
  for (const auto& [header, value] : entry.headers) {
    std::string out;
    std::string_view rest = value;
    for (size_t start = rest.find("${"); start != std::string_view::npos;
         start = rest.find("${")) {
      const size_t end = rest.find('}', start);
      if (end == std::string_view::npos) {
        break;
      }
      out.append(rest.substr(0, start));
      auto field = filled.find(std::string(rest.substr(start + 2, end - start - 2)));
      if (field != filled.end()) {
        out.append(field->second);
      }
      rest.remove_prefix(end + 1);
    }
    out.append(rest);
    if (!base::TrimWhitespaceASCII(out, base::TRIM_ALL).empty()) {
      config.headers[header] = std::move(out);
    }
  }
  return config;
}

bool SameServer(std::string_view a, std::string_view b) {
  return HostAndPath(a) == HostAndPath(b);
}

ConnectorCatalog::ConnectorCatalog() = default;
ConnectorCatalog::ConnectorCatalog(const ConnectorCatalog&) = default;
ConnectorCatalog& ConnectorCatalog::operator=(const ConnectorCatalog&) =
    default;
ConnectorCatalog::ConnectorCatalog(ConnectorCatalog&&) = default;
ConnectorCatalog& ConnectorCatalog::operator=(ConnectorCatalog&&) = default;
ConnectorCatalog::~ConnectorCatalog() = default;

// static
std::optional<ConnectorCatalog> ConnectorCatalog::FromJson(
    std::string_view json) {
  std::optional<base::DictValue> saved =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!saved) {
    return std::nullopt;
  }
  ConnectorCatalog catalog;
  catalog.refreshed = TimeFrom(*saved, "refreshed");
  catalog.registry_since = TimeFrom(*saved, "registry_since");
  catalog.registry_pass = TimeFrom(*saved, "registry_pass");
  catalog.registry_cursor = Text(*saved, "registry_cursor");
  const base::ListValue* registry = saved->FindList("registry");
  for (const base::Value& each : ListOf(registry)) {
    if (std::optional<CatalogEntry> entry = EntryFromValue(each)) {
      std::string id = entry->id;
      catalog.registry_.emplace(std::move(id), std::move(*entry));
    }
  }
  const base::ListValue* plugins = saved->FindList("plugins");
  for (const base::Value& each : ListOf(plugins)) {
    const base::DictValue* dict = each.GetIfDict();
    const base::Value* entry_value = dict ? dict->Find("entry") : nullptr;
    std::optional<CatalogEntry> entry =
        entry_value ? EntryFromValue(*entry_value) : std::nullopt;
    if (entry) {
      catalog.plugins_.emplace_back(Text(*dict, "folder"), std::move(*entry));
    }
  }
  return catalog;
}

std::string ConnectorCatalog::ToJson() const {
  base::ListValue registry;
  for (const auto& [id, entry] : registry_) {
    registry.Append(EntryToValue(entry));
  }
  base::ListValue plugins;
  for (const auto& [folder, entry] : plugins_) {
    plugins.Append(
        base::DictValue().Set("folder", folder).Set("entry", EntryToValue(entry)));
  }
  return base::WriteJson(base::DictValue()
                             .Set("refreshed", TimeValue(refreshed))
                             .Set("registry_since", TimeValue(registry_since))
                             .Set("registry_pass", TimeValue(registry_pass))
                             .Set("registry_cursor", registry_cursor)
                             .Set("registry", std::move(registry))
                             .Set("plugins", std::move(plugins)))
      .value_or("{}");
}

void ConnectorCatalog::AddRegistryPage(RegistryPage page) {
  for (const std::string& id : page.removed) {
    registry_.erase(id);
  }
  for (CatalogEntry& entry : page.entries) {
    std::string id = entry.id;
    registry_.insert_or_assign(std::move(id), std::move(entry));
  }
}

void ConnectorCatalog::SetPlugins(
    std::vector<std::pair<std::string, CatalogEntry>> plugins) {
  plugins_ = std::move(plugins);
}

const CatalogEntry* ConnectorCatalog::PinnedPlugin(
    std::string_view folder) const {
  if (!IsPinned(folder)) {
    return nullptr;
  }
  for (const auto& [read_from, entry] : plugins_) {
    if (read_from == folder) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<CatalogEntry> ConnectorCatalog::Search(std::string_view query,
                                                   size_t limit) const {
  // Every entry in rank order, each pointing at the first of its server.
  std::vector<const CatalogEntry*> all;
  for (const CatalogEntry& entry : RecommendedConnectors()) {
    all.push_back(&entry);
  }
  for (CatalogSource source :
       {CatalogSource::kClaudePlugin, CatalogSource::kOpenAiPlugin}) {
    for (const auto& [folder, entry] : plugins_) {
      if (entry.source == source) {
        all.push_back(&entry);
      }
    }
  }
  for (const auto& [id, entry] : registry_) {
    all.push_back(&entry);
  }
  std::map<std::string, size_t> first_of_server;
  std::map<std::string, size_t> first_plugin_of_name;
  std::vector<size_t> winner(all.size());
  for (size_t i = 0; i < all.size(); ++i) {
    const CatalogEntry& entry = *all[i];
    winner[i] = first_of_server.emplace(HostAndPath(entry.url), i).first->second;
    if (entry.source != CatalogSource::kRegistry) {
      // A plugin named as a recommended connector, or as another plugin, is
      // the same service.
      auto named = first_plugin_of_name.emplace(entry.name, i).first;
      winner[i] = std::min(winner[i], named->second);
    }
    // An earlier entry already points at the first of its server.
    winner[i] = winner[winner[i]];
  }

  const std::vector<std::string> words = base::SplitString(
      base::ToLowerASCII(query), " \t", base::TRIM_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);
  // Lower is better: the title is the query, starts with it, holds it, or
  // only the rest of the entry holds it.
  std::map<size_t, int> found;
  for (size_t i = 0; i < all.size(); ++i) {
    const CatalogEntry& entry = *all[i];
    int score = 0;
    if (!words.empty()) {
      const std::string title = base::ToLowerASCII(entry.title);
      const std::string text = base::ToLowerASCII(
          base::StrCat({entry.title, " ", entry.name, " ", entry.description,
                        " ", GURL(entry.website).host(), " ",
                        GURL(entry.url).host()}));
      if (std::ranges::any_of(words, [&](const std::string& word) {
            return !text.contains(word);
          })) {
        continue;
      }
      const std::string whole = base::JoinString(words, " ");
      score = title == whole || entry.name == whole ? 0
              : title.starts_with(words[0])         ? 1
              : title.contains(words[0])            ? 2
                                                    : 3;
    } else if (entry.source != CatalogSource::kRecommended) {
      continue;
    }
    auto [at, added] = found.emplace(winner[i], score);
    if (!added) {
      at->second = std::min(at->second, score);
    }
  }
  std::vector<std::pair<int, size_t>> ranked;
  for (const auto& [index, score] : found) {
    ranked.emplace_back(score, index);
  }
  std::ranges::sort(ranked);
  std::vector<CatalogEntry> entries;
  for (const auto& [score, index] : ranked) {
    if (entries.size() == limit) {
      break;
    }
    entries.push_back(*all[index]);
  }
  return entries;
}

std::optional<CatalogEntry> ConnectorCatalog::Find(std::string_view id) const {
  for (const CatalogEntry& entry : RecommendedConnectors()) {
    if (entry.id == id) {
      return entry;
    }
  }
  for (const auto& [folder, entry] : plugins_) {
    if (entry.id == id) {
      return entry;
    }
  }
  if (auto found = registry_.find(id); found != registry_.end()) {
    return found->second;
  }
  return std::nullopt;
}

}  // namespace uw
