// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/ai_providers/catalog.h"

#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"

namespace uw {
namespace {

constexpr int kSavedVersion = 1;

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

// One model of api.json as uw keeps it. Only named effort levels count.
base::DictValue Abilities(const base::DictValue& model) {
  base::ListValue efforts;
  if (const base::ListValue* options = model.FindList("reasoning_options")) {
    for (const base::Value& option : *options) {
      const base::DictValue* dict = option.GetIfDict();
      const std::string* type = dict ? dict->FindString("type") : nullptr;
      const base::ListValue* values = dict ? dict->FindList("values") : nullptr;
      if (type && *type == "effort" && values) {
        for (const base::Value& value : *values) {
          if (value.is_string()) {
            efforts.Append(value.GetString());
          }
        }
      }
    }
  }
  return base::DictValue()
      .Set("tools", model.FindBool("tool_call").value_or(true))
      .Set("efforts", std::move(efforts));
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
  for (ProviderPreset preset :
       {ProviderPreset::kOpenAi, ProviderPreset::kAnthropic,
        ProviderPreset::kOpenRouter, ProviderPreset::kVercelAiGateway}) {
    const std::string_view key = *CatalogKey(preset);
    const base::DictValue* provider = all->FindDict(key);
    const base::DictValue* models =
        provider ? provider->FindDict("models") : nullptr;
    if (!models) {
      continue;
    }
    base::DictValue kept;
    for (const auto [id, model] : *models) {
      if (model.is_dict()) {
        kept.Set(id, Abilities(model.GetDict()));
      }
    }
    catalog.models_.Set(key, std::move(kept));
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
  base::DictValue* models = saved && saved->FindInt("version") == kSavedVersion
                                ? saved->FindDict("models")
                                : nullptr;
  if (!models) {
    return std::nullopt;
  }
  ModelCatalog catalog;
  catalog.models_ = std::move(*models);
  return catalog;
}

std::string ModelCatalog::ToSaved() const {
  return base::WriteJson(base::DictValue()
                             .Set("version", kSavedVersion)
                             .Set("models", models_.Clone()))
      .value_or("");
}

ModelAbilities ModelCatalog::For(const Provider& provider,
                                 std::string_view model) const {
  const std::optional<std::string_view> key = CatalogKey(provider.preset);
  const base::DictValue* models = key ? models_.FindDict(*key) : nullptr;
  const base::DictValue* known = models ? models->FindDict(model) : nullptr;
  if (!known) {
    return UnknownModelAbilities(provider);
  }
  ModelAbilities abilities;
  abilities.calls_tools = known->FindBool("tools").value_or(true);
  if (const base::ListValue* efforts = known->FindList("efforts")) {
    for (const base::Value& effort : *efforts) {
      if (effort.is_string()) {
        abilities.efforts.push_back(effort.GetString());
      }
    }
  }
  return abilities;
}

bool CatalogCovers(ProviderPreset preset) {
  return CatalogKey(preset).has_value();
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
