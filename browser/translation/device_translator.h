// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_DEVICE_TRANSLATOR_H_
#define UW_BROWSER_TRANSLATION_DEVICE_TRANSLATOR_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/types/expected.h"
#include "components/on_device_translation/installer.h"
#include "components/on_device_translation/public/mojom/translator.mojom.h"
#include "components/on_device_translation/service_controller.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/translation/segment_translator.h"

namespace content {
class BrowserContext;
}

namespace uw {

// Translates with Chromium's on-device engine, TranslateKit, which the
// component updater downloads with a language pack per language. Keeps one
// translator for the last pair of languages.
class DeviceTranslator
    : public SegmentTranslator,
      public on_device_translation::OnDeviceTranslationInstaller::Observer {
 public:
  // Reports the download of a language pack: the language it adds and the
  // percentage done.
  using DownloadCallback =
      base::RepeatingCallback<void(const std::string& language, int percent)>;

  DeviceTranslator(content::BrowserContext* context,
                   DownloadCallback on_download);
  ~DeviceTranslator() override;

  // The engine's code for a translate language code, or nullopt when the
  // engine does not know the language.
  static std::optional<std::string> LanguageCode(std::string_view language);

  // SegmentTranslator:
  void Translate(std::vector<std::string> segments,
                 const std::string& source_language,
                 const std::string& target_language,
                 ResultCallback callback) override;

 private:
  struct Request {
    Request(std::vector<std::string> segments, ResultCallback callback);
    Request(Request&&);
    Request& operator=(Request&&);
    ~Request();
    std::vector<std::string> segments;
    ResultCallback callback;
  };

  void OnCanTranslate(
      on_device_translation::OnDeviceTranslationController::CanTranslateResult
          result);
  void OnTranslatorCreated(
      base::expected<
          mojo::PendingRemote<on_device_translation::mojom::OnDeviceTranslator>,
          on_device_translation::OnDeviceTranslationController::
              CreateTranslatorError> result);
  void Run(Request request);
  void FailAll(Result result);

  // OnDeviceTranslationInstaller::Observer:
  void OnLanguagePackInstalled(
      on_device_translation::LanguagePackKey language_pack) override;
  void OnLanguagePackInstallationChanged(
      on_device_translation::LanguagePackKey language_pack) override;
  void OnInstallationChanged() override;
  void OnLanguagePackProgress(
      on_device_translation::LanguagePackKey language_pack,
      int progress) override;

  raw_ptr<content::BrowserContext> context_;
  DownloadCallback on_download_;
  std::string source_;
  std::string target_;
  std::set<on_device_translation::LanguagePackKey> downloading_;
  bool creating_ = false;
  std::vector<Request> waiting_;
  mojo::Remote<on_device_translation::mojom::OnDeviceTranslator> translator_;
  base::ScopedObservation<
      on_device_translation::OnDeviceTranslationInstaller,
      on_device_translation::OnDeviceTranslationInstaller::Observer>
      installer_observation_{this};
  base::WeakPtrFactory<DeviceTranslator> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_DEVICE_TRANSLATOR_H_
