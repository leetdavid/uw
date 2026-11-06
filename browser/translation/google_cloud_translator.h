// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_GOOGLE_CLOUD_TRANSLATOR_H_
#define UW_BROWSER_TRANSLATION_GOOGLE_CLOUD_TRANSLATOR_H_

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "uw/browser/translation/segment_translator.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace uw {

// Translates with Google Cloud Translation, the Basic edition's v2 API, with
// the user's own API key. The key never reaches Chromium's own Google key.
// See "Settings" in docs/plans/page-translation.md.
class GoogleCloudTranslator : public SegmentTranslator {
 public:
  // The key's account in the key store, which every profile shares.
  static constexpr char kKeyAccount[] = "translation-google-cloud";

  // Translates one word with `key`, and answers nullopt when it worked, or
  // Google's words about why it didn't.
  static void CheckKey(
      scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
      const std::string& key,
      base::OnceCallback<void(std::optional<std::string>)> callback);

  GoogleCloudTranslator(
      scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
      std::string key);
  ~GoogleCloudTranslator() override;

  // SegmentTranslator:
  void Translate(std::vector<std::string> segments,
                 const std::string& source_language,
                 const std::string& target_language,
                 ResultCallback callback) override;

 private:
  using Loaders = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  void OnResponse(Loaders::iterator loader,
                  size_t count,
                  ResultCallback callback,
                  std::optional<std::string> body);

  scoped_refptr<network::SharedURLLoaderFactory> loader_factory_;
  // Never logged.
  const std::string key_;
  Loaders loaders_;
  base::WeakPtrFactory<GoogleCloudTranslator> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_GOOGLE_CLOUD_TRANSLATOR_H_
