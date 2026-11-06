// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/on_device_translation/installer.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {

class ModelLister;
struct ModelListing;

// Settings › Languages › Translate: the languages this device has downloaded
// for translating pages, the Google Cloud Translation key, and the language
// model that translates for the AI provider engine. See "Settings" in
// docs/plans/page-translation.md.
class TranslationSettingsHandler
    : public content::WebUIMessageHandler,
      public on_device_translation::OnDeviceTranslationInstaller::Observer {
 public:
  TranslationSettingsHandler();
  ~TranslationSettingsHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 private:
  // Sizes of the downloaded languages: each language's code and bytes.
  using Sizes = std::vector<std::pair<std::string, int64_t>>;

  void HandleGetLanguages(const base::ListValue& args);
  void HandleRemoveLanguage(const base::ListValue& args);
  void HandleGetGoogleCloudKey(const base::ListValue& args);
  void HandleSaveGoogleCloudKey(const base::ListValue& args);
  void HandleRemoveGoogleCloudKey(const base::ListValue& args);
  void HandleGetModels(const base::ListValue& args);
  void HandleSetModel(const base::ListValue& args);
  void OnKeyChecked(const std::string& callback_id,
                    const std::string& key,
                    std::optional<std::string> problem);
  void OnListing(size_t id, const Provider& provider, ModelListing listing);
  void AnswerModels(size_t id);
  void OnSizes(const std::string& callback_id,
               const std::string& target,
               Sizes sizes);
  void NotifyChanged();

  // OnDeviceTranslationInstaller::Observer:
  void OnLanguagePackInstalled(
      on_device_translation::LanguagePackKey language_pack) override;
  void OnLanguagePackInstallationChanged(
      on_device_translation::LanguagePackKey language_pack) override;
  void OnInstallationChanged() override;

  // A request for the models, waiting for each provider's list.
  struct ModelsRequest {
    ModelsRequest();
    ~ModelsRequest();
    std::string callback_id;
    size_t waiting = 0;
    base::ListValue models;
    std::vector<std::unique_ptr<ModelLister>> listers;
  };
  std::map<size_t, std::unique_ptr<ModelsRequest>> models_requests_;
  size_t next_models_request_ = 0;
  // Each model's lowest effort and format, as the last listing said, for when
  // it is picked. Keyed by provider id and model.
  std::map<std::pair<std::string, std::string>, ModelChoice> listed_;

  base::ScopedObservation<
      on_device_translation::OnDeviceTranslationInstaller,
      on_device_translation::OnDeviceTranslationInstaller::Observer>
      observation_{this};
  base::WeakPtrFactory<TranslationSettingsHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_
