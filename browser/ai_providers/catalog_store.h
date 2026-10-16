// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AI_PROVIDERS_CATALOG_STORE_H_
#define UW_BROWSER_AI_PROVIDERS_CATALOG_STORE_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "url/gurl.h"
#include "uw/components/ai_providers/catalog.h"

class Profile;

namespace network {
class SimpleURLLoader;
}

namespace uw {

// Keeps a profile's copy of the models.dev catalog: read from the profile
// folder, downloaded the first time a page needs it and again once a day,
// and kept as it is when a download fails. The request carries nothing about
// the user. See ADR 0008.
class CatalogStore : public base::SupportsUserData::Data {
 public:
  using CatalogCallback = base::OnceCallback<void(const ModelCatalog*)>;

  // Made the first time a page asks.
  static CatalogStore* For(Profile* profile);
  // Where tests serve the catalog from.
  static void SetSourceForTesting(const GURL& url);

  CatalogStore(const CatalogStore&) = delete;
  CatalogStore& operator=(const CatalogStore&) = delete;
  ~CatalogStore() override;

  // Runs `done` with the catalog, or with nullptr while uw has none because
  // the first download failed.
  void Get(CatalogCallback done);

  // Runs `done` with the format `model` goes over at `provider`, to keep
  // with a pick: its maker's when the provider leaves it to each model,
  // nothing otherwise.
  using FormatCallback =
      base::OnceCallback<void(std::optional<ProviderFormat>)>;
  void FormatOf(const Provider& provider,
                std::string model,
                FormatCallback done);

 private:
  explicit CatalogStore(Profile* profile);

  struct Saved;
  void OnRead(std::optional<Saved> saved);
  void Download();
  void OnDownloaded(std::optional<std::string> body);
  void OnParsed(std::optional<ModelCatalog> catalog);
  void Answer();

  const raw_ptr<Profile> profile_;
  const base::FilePath path_;
  std::optional<ModelCatalog> catalog_;
  base::Time fetched_;
  bool reading_ = false;
  bool read_ = false;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  std::vector<CatalogCallback> waiting_;
  base::WeakPtrFactory<CatalogStore> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_CATALOG_STORE_H_
