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
#include "uw/components/tab_organization/ai_protocol.h"

class GURL;
class Profile;

namespace content {
class WebUIDataSource;
}  // namespace content

namespace uw {

// Mojo backend for the uw page of chrome://settings: the models behind
// automatic tab organization, its pause, and the window tint.
// Profile-scoped: it touches profile prefs, the profile's theme color, and
// the profile's Keychain slots, never window state, so closing windows while
// settings is open is safe.
// API keys stay in the Keychain and never reach prefs, logs, or the page.
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
  void LoadModels(bool decision,
                  mojom::ConnectionInputPtr connection,
                  LoadModelsCallback callback) override;
  void Save(mojom::ConnectionInputPtr decision,
            mojom::ConnectionInputPtr language,
            SaveCallback callback) override;
  void SetPause(int32_t mode, SetPauseCallback callback) override;
  void GetTint(GetTintCallback callback) override;
  void SetTint(const std::string& color, SetTintCallback callback) override;
  void ClearTint(ClearTintCallback callback) override;

 private:
  mojom::TintStatePtr CurrentTint() const;
  void StoreDecision(const mojom::ConnectionInput& input);
  void StoreLanguage(const mojom::ConnectionInput& input);
  void CheckDecision();
  // `names_only` is true when a decision model does the deciding.
  void CheckLanguage(bool names_only);
  void OnModelsLoaded(LoadModelsCallback callback,
                      RemoteModelClient::Outcome outcome);
  void OnDecisionChecked(DecisionFormat format,
                         RemoteModelClient::Outcome outcome);
  void OnLanguageChecked(bool names_only, RemoteModelClient::Outcome outcome);
  // Turns automatic organization on when every check passed, then answers.
  void FinishSaveWhenChecked();
  std::unique_ptr<RemoteModelClient> Client(const GURL& url,
                                            std::string api_key,
                                            std::string body);

  const raw_ptr<Profile> profile_;
  mojo::Receiver<mojom::OrganizationHandler> receiver_;
  std::unique_ptr<RemoteModelClient> models_request_;
  std::unique_ptr<RemoteModelClient> decision_check_;
  std::unique_ptr<RemoteModelClient> language_check_;
  mojom::SaveResultPtr save_result_;
  SaveCallback save_callback_;
  base::WeakPtrFactory<UwOrganizationHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
