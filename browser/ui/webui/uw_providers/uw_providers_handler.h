// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_HANDLER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/ai_providers/model_lister.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers.mojom.h"
#include "uw/components/agent/skills.h"
#include "uw/components/ai_providers/providers.h"

class Profile;

namespace content {
class WebContents;
}

namespace uw {

// Mojo backend of uw://ai, and of the chat page's model chips. Profile-scoped:
// it touches profile prefs and the providers' Keychain accounts. Keys never
// reach prefs, logs, or the page.
class UwProvidersHandler : public mojom::ProvidersHandler,
                           public Connectors::Observer {
 public:
  // The request that tests a model, and how its answer is read.
  struct ModelTest;

  // `contents` is the page's tab.
  UwProvidersHandler(Profile* profile,
                     content::WebContents* contents,
                     mojo::PendingReceiver<mojom::ProvidersHandler> receiver);
  UwProvidersHandler(const UwProvidersHandler&) = delete;
  UwProvidersHandler& operator=(const UwProvidersHandler&) = delete;
  ~UwProvidersHandler() override;

  // mojom::ProvidersHandler:
  void GetState(GetStateCallback callback) override;
  void ListModels(const std::string& provider_id,
                  ListModelsCallback callback) override;
  void ListModelsAt(mojom::NewProviderPtr provider,
                    ListModelsAtCallback callback) override;
  void AddProvider(mojom::NewProviderPtr provider,
                   AddProviderCallback callback) override;
  void ReplaceKey(const std::string& provider_id,
                  const std::string& api_key,
                  ReplaceKeyCallback callback) override;
  void ChangeAddress(const std::string& provider_id,
                     const std::string& address,
                     ChangeAddressCallback callback) override;
  void RemoveProvider(const std::string& provider_id,
                      RemoveProviderCallback callback) override;
  void PickModel(mojom::ModelSlot slot,
                 const std::string& provider_id,
                 const std::string& model,
                 PickModelCallback callback) override;
  void ClearModel(mojom::ModelSlot slot, ClearModelCallback callback) override;
  void SetEffort(mojom::ModelSlot slot,
                 const std::string& effort,
                 SetEffortCallback callback) override;
  void TestModel(mojom::ModelSlot slot, TestModelCallback callback) override;
  void DismissUpgradeNotice(DismissUpgradeNoticeCallback callback) override;
  void GetSkills(GetSkillsCallback callback) override;
  void SetSkillOn(const std::string& name, bool on) override;
  void OpenSkillsFolder() override;
  void ShowAiPage(const std::string& section) override;
  void GetConnectors(GetConnectorsCallback callback) override;
  void WaitForConnectorChange(WaitForConnectorChangeCallback callback) override;
  void ReadConnectors(const std::string& json,
                      ReadConnectorsCallback callback) override;
  void AddConnectors(const std::string& json,
                     AddConnectorsCallback callback) override;
  void RemoveConnector(const std::string& name) override;
  void SetConnectorOn(const std::string& name, bool on) override;
  void SetConnectorToolOn(const std::string& name,
                          const std::string& tool,
                          bool on) override;
  void SetToolAlwaysAllowed(const std::string& name,
                            const std::string& tool,
                            bool allowed) override;
  void RestartConnector(const std::string& name) override;
  void SignInConnector(const std::string& name) override;
  void CancelSignIn(const std::string& name) override;
  void SignOutConnector(const std::string& name) override;
  void OpenConnectorInTerminal(const std::string& name) override;

  // Connectors::Observer:
  void OnConnectorsChanged() override;

 private:
  // A request the page waits on. Several model lists can be on their way at
  // once, one per provider.
  scoped_refptr<network::SharedURLLoaderFactory> LoaderFactory();
  void Send(std::unique_ptr<RemoteModelClient> client,
            bool post,
            RemoteModelClient::ResultCallback done);
  void OnAnswered(int request,
                  RemoteModelClient::ResultCallback done,
                  RemoteModelClient::Outcome outcome);
  void OnTested(ModelSlot slot,
                ModelChoice choice,
                ModelTest test,
                std::string key,
                base::TimeTicks sent_at,
                TestModelCallback callback,
                RemoteModelClient::Outcome outcome);

  void OnPickFormat(ModelSlot slot,
                    const std::string& provider_id,
                    const std::string& model,
                    std::optional<ProviderFormat> format);
  void OnAddressListed(Provider moved,
                       ChangeAddressCallback callback,
                       ModelListing listing);

  void ReplySkills(GetSkillsCallback callback, std::vector<Skill> skills);

  const raw_ptr<Profile> profile_;
  const raw_ptr<content::WebContents> contents_;
  std::vector<WaitForConnectorChangeCallback> waiting_for_change_;
  base::ScopedObservation<Connectors, Connectors::Observer>
      connectors_observation_{this};
  mojo::Receiver<mojom::ProvidersHandler> receiver_;
  ModelLister lister_;
  std::map<int, std::unique_ptr<RemoteModelClient>> requests_;
  int next_request_ = 0;
  base::WeakPtrFactory<UwProvidersHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_HANDLER_H_
