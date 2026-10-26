// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/model_lister.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/containers/fixed_flat_set.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {
namespace {

// Parts of model names that are plainly not for chat.
constexpr auto kNotForChat = base::MakeFixedFlatSet<std::string_view>(
    {"embed", "tts", "whisper", "dall-e", "image", "moderation", "transcribe",
     "speech", "rerank"});

bool ForChat(const std::string& name) {
  const std::string lower = base::ToLowerASCII(name);
  return std::ranges::none_of(kNotForChat, [&](std::string_view part) {
    return lower.find(part) != std::string::npos;
  });
}

// Ollama's own details of a model live beside its OpenAI-compatible
// address: http://localhost:11434/v1 has them at
// http://localhost:11434/api/show.
GURL OllamaShowUrl(std::string_view address) {
  std::string base(address);
  while (base.ends_with('/')) {
    base.pop_back();
  }
  if (base.ends_with("/v1")) {
    base.resize(base.size() - 3);
  }
  return GURL(base + "/api/show");
}

RemoteModelClient::KeyHeader KeyHeaderFor(const Provider& provider) {
  return RemoteModelClient::KeyHeaderFor(provider.preset, provider.format);
}

}  // namespace

struct ModelLister::Listing {
  Provider provider;
  ListingCallback done;
  ModelListing result;
  // Ollama model details still on their way.
  size_t waiting = 0;
};

ModelLister::ModelLister(Profile* profile) : profile_(profile) {}

ModelLister::~ModelLister() = default;

void ModelLister::List(const Provider& provider,
                       std::string key,
                       ListingCallback done) {
  if (!IsValidProviderEndpoint(provider.address)) {
    std::move(done).Run(ModelListing());
    return;
  }
  auto listing = std::make_unique<Listing>();
  listing->provider = provider;
  listing->done = std::move(done);
  Send(std::make_unique<RemoteModelClient>(
           profile_->GetDefaultStoragePartition()
               ->GetURLLoaderFactoryForBrowserProcess(),
           ModelsUrl(provider.address), std::move(key), std::string(),
           kProviderTimeout, KeyHeaderFor(provider)),
       /*post=*/false,
       base::BindOnce(&ModelLister::OnList, weak_factory_.GetWeakPtr(),
                      std::move(listing)));
}

int ModelLister::Send(std::unique_ptr<RemoteModelClient> client,
                      bool post,
                      RemoteModelClient::ResultCallback done) {
  const int request = next_request_++;
  RemoteModelClient* sender = client.get();
  requests_[request] = std::move(client);
  auto answered =
      base::BindOnce(&ModelLister::OnAnswered, weak_factory_.GetWeakPtr(),
                     request, std::move(done));
  if (post) {
    sender->Send(std::move(answered));
  } else {
    sender->SendGet(std::move(answered));
  }
  return request;
}

void ModelLister::OnAnswered(int request,
                             RemoteModelClient::ResultCallback done,
                             RemoteModelClient::Outcome outcome) {
  requests_.erase(request);
  std::move(done).Run(std::move(outcome));
}

void ModelLister::OnList(std::unique_ptr<Listing> listing,
                         RemoteModelClient::Outcome outcome) {
  const std::optional<std::vector<std::u16string>> names =
      outcome.ok ? ParseModelList(outcome.body) : std::nullopt;
  if (!names) {
    std::string error =
        outcome.net_error != net::OK
            ? net::ErrorToShortString(outcome.net_error)
            : base::StrCat({"HTTP ", base::NumberToString(outcome.http_status),
                            " ", ProviderMessage(outcome.body)});
    base::TrimWhitespaceASCII(error, base::TRIM_TRAILING,
                              &listing->result.error);
    std::move(listing->done).Run(std::move(listing->result));
    return;
  }
  listing->result.ok = true;
  for (const std::u16string& name : *names) {
    std::string model = base::UTF16ToUTF8(name);
    if (ForChat(model)) {
      const ProviderFormat format =
          FormatOfModel(listing->provider, model, nullptr);
      listing->result.models.push_back(
          {std::move(model), UnknownModelAbilities(listing->provider), format});
    }
  }
  Listing* raw = listing.get();
  listings_[raw] = std::move(listing);
  if (raw->provider.preset == ProviderPreset::kOllama) {
    raw->waiting = raw->result.models.size();
    if (!raw->waiting) {
      Finish(raw);
      return;
    }
    for (size_t i = 0; i < raw->result.models.size(); ++i) {
      Send(std::make_unique<RemoteModelClient>(
               profile_->GetDefaultStoragePartition()
                   ->GetURLLoaderFactoryForBrowserProcess(),
               OllamaShowUrl(raw->provider.address), std::string(),
               base::WriteJson(
                   base::DictValue().Set("model", raw->result.models[i].name))
                   .value_or("{}"),
               kProviderTimeout),
           /*post=*/true,
           base::BindOnce(&ModelLister::OnOllamaModel,
                          weak_factory_.GetWeakPtr(), raw, i));
    }
    return;
  }
  if (!CatalogCovers(raw->provider.preset)) {
    Finish(raw);
    return;
  }
  // A method on a weak pointer, so that an answer after the lister is gone
  // never touches `raw`.
  CatalogStore::For(profile_)->Get(
      base::BindOnce(&ModelLister::OnCatalog, weak_factory_.GetWeakPtr(), raw));
}

void ModelLister::OnCatalog(Listing* listing, const ModelCatalog* catalog) {
  if (!listings_.contains(listing)) {
    return;
  }
  if (catalog) {
    for (ListedModel& model : listing->result.models) {
      model.abilities = catalog->For(listing->provider, model.name);
      model.format = FormatOfModel(listing->provider, model.name, catalog);
    }
  }
  Finish(listing);
}

// Ollama says what a model can do in "capabilities". Without an answer, the
// model keeps the defaults of one the catalog does not know.
void ModelLister::OnOllamaModel(Listing* listing,
                                size_t index,
                                RemoteModelClient::Outcome outcome) {
  std::optional<base::DictValue> details =
      outcome.ok
          ? base::JSONReader::ReadDict(outcome.body, base::JSON_PARSE_RFC)
          : std::nullopt;
  const base::ListValue* capabilities =
      details ? details->FindList("capabilities") : nullptr;
  if (capabilities) {
    ModelAbilities& abilities = listing->result.models[index].abilities;
    abilities.calls_tools =
        std::ranges::contains(*capabilities, base::Value("tools"));
    abilities.sees_images =
        std::ranges::contains(*capabilities, base::Value("vision"));
    abilities.efforts.clear();
    if (std::ranges::contains(*capabilities, base::Value("thinking"))) {
      abilities.efforts.assign(std::begin(kUsualEfforts),
                               std::end(kUsualEfforts));
    }
  }
  if (--listing->waiting == 0) {
    Finish(listing);
  }
}

void ModelLister::Finish(Listing* listing) {
  auto found = listings_.find(listing);
  std::unique_ptr<Listing> owned = std::move(found->second);
  listings_.erase(found);
  std::move(owned->done).Run(std::move(owned->result));
}

}  // namespace uw
