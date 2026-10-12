// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_

#include <array>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
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
  // What a test of one connection sends and how its answer is judged.
  struct ConnectionTest;

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
  void TestConnection(bool decision,
                      mojom::ConnectionInputPtr connection,
                      bool names_only,
                      TestConnectionCallback callback) override;
  void Save(mojom::ConnectionInputPtr decision,
            mojom::ConnectionInputPtr language,
            SaveCallback callback) override;
  void TurnOff(TurnOffCallback callback) override;
  void SetPause(int32_t mode, SetPauseCallback callback) override;
  void GetTint(GetTintCallback callback) override;
  void SetTint(const std::string& color, SetTintCallback callback) override;
  void ClearTint(ClearTintCallback callback) override;

 private:
  // One test runs per connection at a time.
  enum Slot { kDecisionSlot = 0, kLanguageSlot = 1 };

  mojom::TintStatePtr CurrentTint() const;
  void StoreDecision(const mojom::ConnectionInput& input);
  void StoreLanguage(const mojom::ConnectionInput& input);
  void OnModelsLoaded(LoadModelsCallback callback,
                      RemoteModelClient::Outcome outcome);
  // Sends `test` and answers `done` with what it found. A newer test of
  // the same connection replaces a running one.
  void RunTest(Slot slot, ConnectionTest test, TestConnectionCallback done);
  void OnTested(Slot slot,
                ConnectionTest test,
                base::TimeTicks sent_at,
                TestConnectionCallback done,
                RemoteModelClient::Outcome outcome);
  // Turns automatic organization on when every test of a save passed, then
  // answers the save.
  void OnSaveTested(Slot slot, mojom::TestResultPtr result);
  std::unique_ptr<RemoteModelClient> Client(const GURL& url,
                                            std::string api_key,
                                            std::string body);

  const raw_ptr<Profile> profile_;
  mojo::Receiver<mojom::OrganizationHandler> receiver_;
  std::unique_ptr<RemoteModelClient> models_request_;
  std::array<std::unique_ptr<RemoteModelClient>, 2> tests_;
  int save_tests_left_ = 0;
  mojom::SaveResultPtr save_result_;
  SaveCallback save_callback_;
  base::WeakPtrFactory<UwOrganizationHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
