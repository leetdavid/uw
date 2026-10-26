// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/ui/webui/uw_organization/uw_organization.mojom.h"

class Profile;

namespace content {
class WebUIDataSource;
}  // namespace content

namespace uw {

// Mojo backend for the uw page of chrome://settings: whether automatic tab
// organization runs, its pause, its thresholds, the theme picker, and the
// command bar. Its models are picked on uw://providers.
// Profile-scoped: it touches profile prefs and the profile's theme color,
// never window state, so closing windows while settings is open is safe.
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
  void SetEnabled(bool on, SetEnabledCallback callback) override;
  void SetPause(int32_t mode, SetPauseCallback callback) override;
  void SetThresholds(mojom::ThresholdsPtr thresholds,
                     SetThresholdsCallback callback) override;
  void OpenThemePicker() override;
  void SetGlassBehind(bool on, SetGlassBehindCallback callback) override;
  void SetCommandBar(bool on, SetCommandBarCallback callback) override;
  void SetCurrency(const std::string& code,
                   SetCurrencyCallback callback) override;
  void SetMouseAction(mojom::MouseAction action,
                      bool on,
                      SetMouseActionCallback callback) override;
  void SetMiniPlayerMode(mojom::MiniPlayerMode mode,
                         SetMiniPlayerModeCallback callback) override;
  void SetSiteLive(const std::string& site,
                   bool live,
                   SetSiteLiveCallback callback) override;
  void SetMiniPlayer(mojom::MiniPlayerSwitch which,
                     bool on,
                     SetMiniPlayerCallback callback) override;
  void GetCommand(GetCommandCallback callback) override;
  void InstallCommand(InstallCommandCallback callback) override;
  void UninstallCommand(UninstallCommandCallback callback) override;
  void GetProblemReports(GetProblemReportsCallback callback) override;
  void SetProblemReports(bool send,
                         SetProblemReportsCallback callback) override;

 private:
  // The codes the last exchange rates cover, or none before the first.
  std::vector<std::string> Currencies() const;
  mojom::StatePtr CurrentState() const;
  mojom::ThresholdsPtr CurrentThresholds() const;

  const raw_ptr<Profile> profile_;
  mojo::Receiver<mojom::OrganizationHandler> receiver_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_ORGANIZATION_UW_ORGANIZATION_HANDLER_H_
