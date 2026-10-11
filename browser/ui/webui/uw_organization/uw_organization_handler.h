// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/browser/ui/webui/uw_organization/uw_organization.mojom.h"

class Profile;

namespace content {
class WebUIDataSource;
}  // namespace content

namespace uw {

// Mojo backend for the uw organization section of chrome://settings.
// Profile-scoped: it touches profile prefs and the profile's Keychain slot,
// never window state, so closing windows while settings is open is safe.
// Secrets stay in the OS credential store and never cross into prefs, logs,
// or page state.
class UwOrganizationHandler : public mojom::OrganizationHandler {
 public:
  UwOrganizationHandler(
      Profile* profile,
      mojo::PendingReceiver<mojom::OrganizationHandler> receiver);
  UwOrganizationHandler(const UwOrganizationHandler&) = delete;
  UwOrganizationHandler& operator=(const UwOrganizationHandler&) = delete;
  ~UwOrganizationHandler() override;

  static void AddLoadTimeData(content::WebUIDataSource* data_source);

  // mojom::OrganizationHandler:
  void GetState(GetStateCallback callback) override;
  void LoadModels(const std::string& endpoint,
                  const std::string& api_key,
                  LoadModelsCallback callback) override;
  void Save(int32_t mode,
            const std::string& endpoint,
            const std::string& model,
            const std::string& api_key,
            SaveCallback callback) override;
  void SetPause(int32_t mode, SetPauseCallback callback) override;

 private:
  void OnModelsLoaded(LoadModelsCallback callback,
                      RemoteModelClient::Outcome outcome);
  void OnValidationComplete(SaveCallback callback,
                            RemoteModelClient::Outcome outcome);

  const raw_ptr<Profile> profile_;
  mojo::Receiver<mojom::OrganizationHandler> receiver_;
  std::unique_ptr<RemoteModelClient> models_request_;
  std::unique_ptr<RemoteModelClient> validation_request_;
  base::WeakPtrFactory<UwOrganizationHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
