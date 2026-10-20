// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ai_providers/catalog_store.h"

#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace uw {
namespace {

constexpr char kUserDataKey[] = "uw_catalog_store";
constexpr char kFileName[] = "uw_model_catalog.json";
constexpr base::TimeDelta kRefreshAfter = base::Days(1);
constexpr base::TimeDelta kTimeout = base::Seconds(60);
// The whole catalog was about 5.6 MB on 2026-10-15.
constexpr size_t kMaxCatalogBytes = 32 * 1024 * 1024;

GURL& Source() {
  static base::NoDestructor<GURL> source(GURL("https://models.dev/api.json"));
  return *source;
}

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_model_catalog", R"(
      semantics {
        sender: "uw AI providers"
        description:
          "Downloads the public models.dev catalog of what language models "
          "can do: whether each calls tools, and which effort levels it "
          "takes."
        trigger:
          "uw://ai/providers or a chat's model menu needs the catalog, at most "
          "once a day."
        data: "None. The request carries no cookies and nothing about the user."
        destination: OTHER
        destination_other: "models.dev"
      }
      policy {
        cookies_allowed: NO
        setting: "Only the pages that show AI models ask for it."
        policy_exception_justification: "Not implemented."
      })");

}  // namespace

struct CatalogStore::Saved {
  ModelCatalog catalog;
  base::Time fetched;
};

// static
CatalogStore* CatalogStore::For(Profile* profile) {
  auto* store = static_cast<CatalogStore*>(profile->GetUserData(kUserDataKey));
  if (!store) {
    std::unique_ptr<CatalogStore> made =
        base::WrapUnique(new CatalogStore(profile));
    store = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return store;
}

// static
void CatalogStore::SetSourceForTesting(const GURL& url) {
  Source() = url;
}

CatalogStore::CatalogStore(Profile* profile)
    : profile_(profile), path_(profile->GetPath().AppendASCII(kFileName)) {}

CatalogStore::~CatalogStore() = default;

void CatalogStore::Get(CatalogCallback done) {
  waiting_.push_back(std::move(done));
  if (!read_) {
    if (!reading_) {
      reading_ = true;
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(
              [](const base::FilePath& path) -> std::optional<Saved> {
                std::string json;
                base::File::Info info;
                if (!base::ReadFileToString(path, &json) ||
                    !base::GetFileInfo(path, &info)) {
                  return std::nullopt;
                }
                std::optional<ModelCatalog> catalog =
                    ModelCatalog::FromSaved(json);
                if (!catalog) {
                  return std::nullopt;
                }
                return Saved{std::move(*catalog), info.last_modified};
              },
              path_),
          base::BindOnce(&CatalogStore::OnRead, weak_factory_.GetWeakPtr()));
    }
    return;
  }
  if (catalog_) {
    Answer();
  }
  Download();
}

void CatalogStore::FormatOf(const Provider& provider,
                            std::string model,
                            FormatCallback done) {
  if (provider.format != ProviderFormat::kEachModel) {
    std::move(done).Run(std::nullopt);
    return;
  }
  Get(base::BindOnce(
      [](Provider provider, std::string model, FormatCallback done,
         const ModelCatalog* catalog) {
        std::move(done).Run(FormatOfModel(provider, model, catalog));
      },
      provider, std::move(model), std::move(done)));
}

void CatalogStore::OnRead(std::optional<Saved> saved) {
  reading_ = false;
  read_ = true;
  if (saved) {
    catalog_ = std::move(saved->catalog);
    fetched_ = saved->fetched;
    Answer();
  }
  Download();
}

// Does nothing while a download runs or the copy is less than a day old.
// Pages that wait without a copy get their answer when it ends.
void CatalogStore::Download() {
  if (loader_ || (catalog_ && base::Time::Now() - fetched_ < kRefreshAfter)) {
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = Source();
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader_->SetTimeoutDuration(kTimeout);
  loader_->DownloadToString(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&CatalogStore::OnDownloaded, weak_factory_.GetWeakPtr()),
      kMaxCatalogBytes);
}

void CatalogStore::OnDownloaded(std::optional<std::string> body) {
  if (!body) {
    loader_.reset();
    Answer();
    return;
  }
  // Reading 5 MB of JSON and writing the copy happen off the main thread.
  // The loader stays until then, so no second download starts.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](const base::FilePath& path,
             std::string body) -> std::optional<ModelCatalog> {
            std::optional<ModelCatalog> catalog =
                ModelCatalog::FromModelsDev(body);
            if (catalog) {
              base::WriteFile(path, catalog->ToSaved());
            }
            return catalog;
          },
          path_, std::move(*body)),
      base::BindOnce(&CatalogStore::OnParsed, weak_factory_.GetWeakPtr()));
}

void CatalogStore::OnParsed(std::optional<ModelCatalog> catalog) {
  loader_.reset();
  if (catalog) {
    catalog_ = std::move(*catalog);
    fetched_ = base::Time::Now();
  }
  Answer();
}

void CatalogStore::Answer() {
  std::vector<CatalogCallback> waiting;
  waiting.swap(waiting_);
  for (CatalogCallback& done : waiting) {
    std::move(done).Run(catalog_ ? &*catalog_ : nullptr);
  }
}

}  // namespace uw
