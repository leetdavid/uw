// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/on_device_translation/installer.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace uw {

// Settings › Languages › Translate: the languages this device has downloaded
// for translating pages, and removing them. See "Settings" in
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

  base::ScopedObservation<
      on_device_translation::OnDeviceTranslationInstaller,
      on_device_translation::OnDeviceTranslationInstaller::Observer>
      observation_{this};
  base::WeakPtrFactory<TranslationSettingsHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_TRANSLATION_TRANSLATION_SETTINGS_HANDLER_H_
