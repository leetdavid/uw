// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AI_PROVIDERS_MODEL_LISTER_H_
#define UW_BROWSER_AI_PROVIDERS_MODEL_LISTER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/ai_providers/catalog.h"
#include "uw/components/ai_providers/providers.h"

class Profile;

namespace uw {

struct ListedModel {
  std::string name;
  ModelAbilities abilities;
  // The format requests to the model go over.
  ProviderFormat format = ProviderFormat::kChatCompletions;
};

struct ModelListing {
  bool ok = false;
  // Without embedding, speech, image, and moderation models.
  std::vector<ListedModel> models;
  // What went wrong when `ok` is false, in the provider's words when it gave
  // any.
  std::string error;
};

// Lists a provider's models as its own list says, with what each can do:
// from the models.dev catalog, or for Ollama from its own model details.
// Destroying the lister drops the requests it has out.
class ModelLister {
 public:
  using ListingCallback = base::OnceCallback<void(ModelListing)>;

  explicit ModelLister(Profile* profile);
  ModelLister(const ModelLister&) = delete;
  ModelLister& operator=(const ModelLister&) = delete;
  ~ModelLister();

  // `key` is the provider's key, or one typed before it is saved.
  void List(const Provider& provider, std::string key, ListingCallback done);

 private:
  struct Listing;
  int Send(std::unique_ptr<RemoteModelClient> client,
           bool post,
           RemoteModelClient::ResultCallback done);
  void OnAnswered(int request,
                  RemoteModelClient::ResultCallback done,
                  RemoteModelClient::Outcome outcome);
  void OnList(std::unique_ptr<Listing> listing,
              RemoteModelClient::Outcome outcome);
  void OnOllamaModel(Listing* listing,
                     size_t index,
                     RemoteModelClient::Outcome outcome);
  void OnCatalog(Listing* listing, const ModelCatalog* catalog);
  void Finish(Listing* listing);

  const raw_ptr<Profile> profile_;
  std::map<int, std::unique_ptr<RemoteModelClient>> requests_;
  std::map<Listing*, std::unique_ptr<Listing>> listings_;
  int next_request_ = 0;
  base::WeakPtrFactory<ModelLister> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_MODEL_LISTER_H_
