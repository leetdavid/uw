// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/catalog.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "url/gurl.h"

namespace uw {
namespace {

constexpr int kSavedVersion = 4;
// What an answer may need of a model's context, at most, when the catalog
// gives no input limit of its own.
constexpr int64_t kAnswerRoom = 16000;

constexpr ProviderPreset kCatalogPresets[] = {
    ProviderPreset::kOpenAi, ProviderPreset::kAnthropic,
    ProviderPreset::kOpenRouter, ProviderPreset::kVercelAiGateway};

// The models.dev provider behind each preset that has one.
std::optional<std::string_view> CatalogKey(ProviderPreset preset) {
  switch (preset) {
    case ProviderPreset::kOpenAi:
      return "openai";
    case ProviderPreset::kAnthropic:
      return "anthropic";
    case ProviderPreset::kOpenRouter:
      return "openrouter";
    case ProviderPreset::kVercelAiGateway:
      return "vercel";
    case ProviderPreset::kCustom:
    case ProviderPreset::kOllama:
    case ProviderPreset::kJev:
      return std::nullopt;
  }
}

bool IsPresetKey(std::string_view key) {
  for (ProviderPreset preset : kCatalogPresets) {
    if (*CatalogKey(preset) == key) {
      return true;
    }
  }
  return false;
}

// An address as uw compares them: without a trailing slash.
std::string Trimmed(const GURL& address) {
  std::string spec = address.spec();
  while (spec.ends_with('/')) {
    spec.pop_back();
  }
  return spec;
}

// One model of api.json as uw keeps it. Only named effort levels and the
// thinking toggle count.
base::DictValue Abilities(const base::DictValue& model) {
  base::ListValue efforts;
  bool toggle = false;
  if (const base::ListValue* options = model.FindList("reasoning_options")) {
    for (const base::Value& option : *options) {
      const base::DictValue* dict = option.GetIfDict();
      const std::string* type = dict ? dict->FindString("type") : nullptr;
      const base::ListValue* values = dict ? dict->FindList("values") : nullptr;
      toggle |= type && *type == "toggle";
      if (type && *type == "effort" && values) {
        for (const base::Value& value : *values) {
          if (value.is_string()) {
            efforts.Append(value.GetString());
          }
        }
      }
    }
  }
  const base::ListValue* inputs =
      model.FindListByDottedPath("modalities.input");
  base::DictValue kept =
      base::DictValue()
          .Set("tools", model.FindBool("tool_call").value_or(true))
          .Set("images",
               !inputs || std::ranges::contains(*inputs, base::Value("image")))
          .Set("pdfs",
               inputs && std::ranges::contains(*inputs, base::Value("pdf")))
          .Set("efforts", std::move(efforts))
          .Set("toggle", toggle);
  // Prices above a context size, as some models have, are left out: uw's
  // requests stay well below them.
  const base::DictValue* cost = model.FindDict("cost");
  const std::optional<double> input =
      cost ? cost->FindDouble("input") : std::nullopt;
  const std::optional<double> output =
      cost ? cost->FindDouble("output") : std::nullopt;
  if (input && output) {
    base::DictValue price =
        base::DictValue().Set("input", *input).Set("output", *output);
    for (const char* key : {"cache_read", "cache_write"}) {
      if (std::optional<double> each = cost->FindDouble(key)) {
        price.Set(key, *each);
      }
    }
    kept.Set("price", std::move(price));
  }
  const std::optional<double> limit_input =
      model.FindDoubleByDottedPath("limit.input");
  const std::optional<double> context =
      model.FindDoubleByDottedPath("limit.context");
  const std::optional<double> answer =
      model.FindDoubleByDottedPath("limit.output");
  if (limit_input && *limit_input > 0) {
    kept.Set("input_limit", *limit_input);
  } else if (context && *context > 0) {
    const int64_t room =
        std::min<int64_t>(kAnswerRoom, answer && *answer > 0
                                           ? static_cast<int64_t>(*answer)
                                           : kAnswerRoom);
    kept.Set("input_limit",
             static_cast<double>(static_cast<int64_t>(*context) - room));
  }
  return kept;
}

}  // namespace

ModelCatalog::ModelCatalog() = default;
ModelCatalog::ModelCatalog(ModelCatalog&&) = default;
ModelCatalog& ModelCatalog::operator=(ModelCatalog&&) = default;
ModelCatalog::~ModelCatalog() = default;

// static
std::optional<ModelCatalog> ModelCatalog::FromModelsDev(
    std::string_view api_json) {
  std::optional<base::DictValue> all =
      base::JSONReader::ReadDict(api_json, base::JSON_PARSE_RFC);
  if (!all) {
    return std::nullopt;
  }
  ModelCatalog catalog;
  for (const auto [key, entry] : *all) {
    const base::DictValue* provider = entry.GetIfDict();
    const base::DictValue* models =
        provider ? provider->FindDict("models") : nullptr;
    const std::string* address =
        provider ? provider->FindString("api") : nullptr;
    if (!models || (!address && !IsPresetKey(key))) {
      continue;
    }
    base::DictValue kept;
    for (const auto [id, model] : *models) {
      if (model.is_dict()) {
        kept.Set(id, Abilities(model.GetDict()));
      }
    }
    catalog.models_.Set(key, std::move(kept));
    if (address) {
      catalog.addresses_.Set(key, *address);
    }
  }
  if (catalog.models_.empty()) {
    return std::nullopt;
  }
  return catalog;
}

// static
std::optional<ModelCatalog> ModelCatalog::FromSaved(std::string_view json) {
  std::optional<base::DictValue> saved =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const bool current = saved && saved->FindInt("version") == kSavedVersion;
  base::DictValue* models = current ? saved->FindDict("models") : nullptr;
  base::DictValue* addresses = current ? saved->FindDict("addresses") : nullptr;
  if (!models || !addresses) {
    return std::nullopt;
  }
  ModelCatalog catalog;
  catalog.models_ = std::move(*models);
  catalog.addresses_ = std::move(*addresses);
  return catalog;
}

std::string ModelCatalog::ToSaved() const {
  return base::WriteJson(base::DictValue()
                             .Set("version", kSavedVersion)
                             .Set("models", models_.Clone())
                             .Set("addresses", addresses_.Clone()))
      .value_or("");
}

std::optional<std::string> ModelCatalog::KeyAt(std::string_view address) const {
  const GURL wanted(address);
  if (!wanted.is_valid()) {
    return std::nullopt;
  }
  std::optional<std::string> on_host;
  int hosts = 0;
  for (const auto [key, value] : addresses_) {
    const GURL known(value.is_string() ? value.GetString() : std::string());
    if (!known.is_valid()) {
      continue;
    }
    if (Trimmed(known) == Trimmed(wanted)) {
      return key;
    }
    if (known.host() == wanted.host()) {
      ++hosts;
      on_host = key;
    }
  }
  return hosts == 1 ? on_host : std::nullopt;
}

ModelAbilities ModelCatalog::For(const Provider& provider,
                                 std::string_view model) const {
  std::optional<std::string> key;
  if (provider.format == ProviderFormat::kEachModel) {
    key = MakerKeyOf(model);
  } else if (provider.preset == ProviderPreset::kCustom) {
    key = KeyAt(provider.address);
  } else if (const std::optional<std::string_view> preset_key =
                 CatalogKey(provider.preset)) {
    key = std::string(*preset_key);
  }
  const base::DictValue* models = key ? models_.FindDict(*key) : nullptr;
  const base::DictValue* known = models ? models->FindDict(model) : nullptr;
  if (!known) {
    return UnknownModelAbilities(provider);
  }
  ModelAbilities abilities;
  abilities.calls_tools = known->FindBool("tools").value_or(true);
  abilities.sees_images = known->FindBool("images").value_or(true);
  abilities.reads_pdfs = known->FindBool("pdfs").value_or(false);
  abilities.toggles_thinking = known->FindBool("toggle").value_or(false);
  if (const base::ListValue* efforts = known->FindList("efforts")) {
    for (const base::Value& effort : *efforts) {
      if (effort.is_string()) {
        abilities.efforts.push_back(effort.GetString());
      }
    }
  }
  if (const base::DictValue* price = known->FindDict("price")) {
    abilities.price = ModelPrice{
        .input = price->FindDouble("input").value_or(0),
        .output = price->FindDouble("output").value_or(0),
        .cache_read = price->FindDouble("cache_read"),
        .cache_write = price->FindDouble("cache_write")};
  }
  abilities.input_limit = std::max<int64_t>(
      0, static_cast<int64_t>(known->FindDouble("input_limit").value_or(0)));
  return abilities;
}

std::optional<std::string> ModelCatalog::MakerKeyOf(
    std::string_view model) const {
  for (const char* maker : {"anthropic", "openai"}) {
    const base::DictValue* models = models_.FindDict(maker);
    if (models && models->FindDict(model)) {
      return maker;
    }
  }
  return std::nullopt;
}

ProviderFormat ModelCatalog::MakerFormatOf(std::string_view model) const {
  const std::optional<std::string> maker = MakerKeyOf(model);
  if (!maker) {
    return ProviderFormat::kChatCompletions;
  }
  return *maker == "anthropic" ? ProviderFormat::kAnthropicMessages
                               : ProviderFormat::kOpenAiResponses;
}

ProviderFormat FormatOfModel(const Provider& provider,
                             std::string_view model,
                             const ModelCatalog* catalog) {
  if (provider.format != ProviderFormat::kEachModel) {
    return LanguageFormatOf(provider).value_or(provider.format);
  }
  return catalog ? catalog->MakerFormatOf(model)
                 : ProviderFormat::kChatCompletions;
}

bool CatalogCovers(ProviderPreset preset) {
  return preset == ProviderPreset::kCustom || CatalogKey(preset).has_value();
}

ModelAbilities UnknownModelAbilities(const Provider& provider) {
  ModelAbilities abilities;
  if (LanguageFormatOf(provider) == ProviderFormat::kChatCompletions) {
    abilities.efforts.assign(std::begin(kUsualEfforts),
                             std::end(kUsualEfforts));
  }
  return abilities;
}

}  // namespace uw
