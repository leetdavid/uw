// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AI_PROVIDERS_CATALOG_H_
#define UW_COMPONENTS_AI_PROVIDERS_CATALOG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/ai_providers/usage.h"

namespace uw {

// What a model can do, as uw shows and uses it. See ADR 0008.
struct ModelAbilities {
  bool calls_tools = true;
  // Whether it takes images. A model the catalog does not know is taken to.
  bool sees_images = true;
  // Whether it reads a PDF whole, pages and pictures, as the catalog says.
  // One the catalog does not know gets a PDF's text instead.
  bool reads_pdfs = false;
  // The effort levels the model takes, named as its provider names them.
  // Empty when it takes none.
  std::vector<std::string> efforts;
  // Whether its thinking can be switched off, as the catalog's toggle says.
  bool toggles_thinking = false;
  // Its price, when the catalog lists one.
  std::optional<ModelPrice> price;
  // How many input tokens a request may hold: the catalog's input limit, or
  // its context limit less what the answer needs. 0 when the catalog does
  // not say.
  int64_t input_limit = 0;
};

// The levels of a model the catalog does not know, over chat completions.
inline constexpr char const* kUsualEfforts[] = {"low", "medium", "high"};

// The parts of the models.dev catalog uw reads: for each provider uw has a
// preset for, and each provider with an address, each model's tool calls and
// effort levels. uw downloads https://models.dev/api.json and keeps this
// smaller copy of it.
class ModelCatalog {
 public:
  ModelCatalog();
  ModelCatalog(ModelCatalog&&);
  ModelCatalog& operator=(ModelCatalog&&);
  ~ModelCatalog();

  // Reads models.dev's api.json. Nothing for a body that is not one.
  static std::optional<ModelCatalog> FromModelsDev(std::string_view api_json);
  // Reads and writes uw's own copy.
  static std::optional<ModelCatalog> FromSaved(std::string_view json);
  std::string ToSaved() const;

  // What `model` at `provider` can do: the catalog's word when it knows the
  // model. A model it does not know calls tools, and takes the usual levels
  // over chat completions and none over the other formats. A model that only
  // switches thinking on or off, or takes a token budget, has no levels.
  // A Custom provider reads the catalog provider at its address.
  ModelAbilities For(const Provider& provider, std::string_view model) const;

  // The format of `model`'s maker: Anthropic's models go over Anthropic
  // Messages, OpenAI's over OpenAI Responses, and any other over chat
  // completions.
  ProviderFormat MakerFormatOf(std::string_view model) const;

 private:
  // The catalog provider at `address`: the one with the same address, else
  // the only one on the same host.
  std::optional<std::string> KeyAt(std::string_view address) const;
  // "anthropic" or "openai" when that maker's entry lists `model`.
  std::optional<std::string> MakerKeyOf(std::string_view model) const;

  // {"openai": {"gpt-5": {"tools": true, "efforts": ["low", ...]}}}
  base::DictValue models_;
  // {"deepseek": "https://api.deepseek.com"}
  base::DictValue addresses_;
};

// Whether uw looks up the models of providers made from `preset` in the
// catalog. A Custom provider is looked up by its address.
bool CatalogCovers(ProviderPreset preset);

// The format `model` goes over at `provider`: its maker's when the provider
// leaves it to each model, from `catalog` while there is one; the provider's
// own otherwise.
ProviderFormat FormatOfModel(const Provider& provider,
                             std::string_view model,
                             const ModelCatalog* catalog);

// What a model the catalog does not know can do at `provider`.
ModelAbilities UnknownModelAbilities(const Provider& provider);

}  // namespace uw

#endif  // UW_COMPONENTS_AI_PROVIDERS_CATALOG_H_
