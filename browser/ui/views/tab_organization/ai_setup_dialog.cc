// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/ai_setup_dialog.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/models/combobox_model_observer.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/view.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {
namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kModeField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kEndpointField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kModelField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKeyField);

// Fixed Keychain account slot; the slot (not the secret) is unremarkable.
constexpr char kOrganizationAccount[] = "organization";
constexpr base::TimeDelta kModelsTimeout = base::Seconds(15);

// Ordered list whose first entry is the initial selection. Disabled entries
// carry load feedback and cannot be picked.
class PickListModel : public ui::ComboboxModel {
 public:
  PickListModel(std::vector<std::u16string> items, std::vector<bool> enabled)
      : items_(std::move(items)), enabled_(std::move(enabled)) {}
  PickListModel(const PickListModel&) = delete;
  PickListModel& operator=(const PickListModel&) = delete;

  void SetItems(std::vector<std::u16string> items, std::vector<bool> enabled) {
    items_ = std::move(items);
    enabled_ = std::move(enabled);
    for (auto& observer : observers()) {
      observer.OnComboboxModelChanged(this);
    }
  }

  bool PickableAt(size_t index) const {
    return index < items_.size() && enabled_[index];
  }
  std::u16string LabelAt(size_t index) const { return items_[index]; }

  // ui::ComboboxModel:
  size_t GetItemCount() const override { return items_.size(); }
  std::u16string GetItemAt(size_t index) const override { return items_[index]; }
  std::optional<size_t> GetDefaultIndex() const override {
    return items_.empty() ? std::nullopt : std::optional<size_t>(0);
  }
  bool IsItemEnabledAt(size_t index) const override { return enabled_[index]; }

 private:
  std::vector<std::u16string> items_;
  std::vector<bool> enabled_;
};

class AiSetupDelegate : public ui::DialogModelDelegate {
 public:
  AiSetupDelegate(base::WeakPtr<TabOrganizationController> controller,
                  Profile* profile,
                  ConnectionMode mode,
                  std::u16string saved_model)
      : controller_(std::move(controller)),
        profile_(profile),
        mode_(mode),
        saved_model_(std::move(saved_model)) {}

  void SetModelList(PickListModel* model_list) { model_list_ = model_list; }

  // Fetches the endpoint's model list and fills the model choices. Matches the
  // dialog menu-item callback signature.
  void LoadModels(int event_flags) {
    if (!profile_ || !model_list_) {
      return;
    }
    const std::string endpoint = base::UTF16ToUTF8(
        dialog_model()->GetTextfieldByUniqueId(kEndpointField)->text());
    if (!IsValidProviderEndpoint(endpoint)) {
      SetFeedback(IDS_UW_AUTO_SETUP_MODELS_ERROR);
      return;
    }
    // The typed key (empty in Ollama mode) authorizes the listing request.
    const std::string api_key = base::UTF16ToUTF8(
        dialog_model()->GetPasswordFieldByUniqueId(kKeyField)->text());
    models_request_ = std::make_unique<RemoteModelClient>(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess(),
        ModelsUrl(endpoint), api_key, std::string(), kModelsTimeout);
    models_request_->Send(
        base::BindOnce(&AiSetupDelegate::OnModels, weak_factory_.GetWeakPtr()));
  }

  // Validates and saves the connection, keeping the dialog open on bad input.
  bool Save() {
    if (!controller_ || !profile_ || !model_list_) {
      return false;
    }
    const ConnectionMode mode = SelectedMode();
    const std::string endpoint = base::UTF16ToUTF8(
        dialog_model()->GetTextfieldByUniqueId(kEndpointField)->text());
    const size_t index =
        dialog_model()->GetComboboxByUniqueId(kModelField)->selected_index();
    if (!IsValidProviderEndpoint(endpoint) || !model_list_->PickableAt(index)) {
      return false;
    }
    const std::string model = base::UTF16ToUTF8(model_list_->LabelAt(index));
    const std::string key = base::UTF16ToUTF8(
        dialog_model()->GetPasswordFieldByUniqueId(kKeyField)->text());
    PrefService* prefs = profile_->GetPrefs();
    ApiKeyStore keys;
    if (mode == ConnectionMode::kOpenAiCompatible) {
      if (!key.empty()) {
        if (!keys.SetKey(kOrganizationAccount, key)) {
          return false;
        }
      } else if (prefs->GetString(kOrganizationKeyAccountPref) !=
                     kOrganizationAccount ||
                 !keys.GetKey(kOrganizationAccount)) {
        // No typed key and no stored key to keep.
        return false;
      }
      prefs->SetString(kOrganizationKeyAccountPref, kOrganizationAccount);
    } else {
      keys.DeleteKey(kOrganizationAccount);
      prefs->SetString(kOrganizationKeyAccountPref, std::string());
    }
    prefs->SetInteger(kOrganizationModePref, static_cast<int>(mode));
    prefs->SetString(kOrganizationEndpointPref, endpoint);
    prefs->SetString(kOrganizationModelPref, model);
    controller_->ValidateProviders();
    return true;
  }

 private:
  ConnectionMode SelectedMode() {
    return dialog_model()->GetComboboxByUniqueId(kModeField)->selected_index() ==
                   0
               ? mode_
               : (mode_ == ConnectionMode::kOllama
                      ? ConnectionMode::kOpenAiCompatible
                      : ConnectionMode::kOllama);
  }

  void SetFeedback(int message_id) {
    model_list_->SetItems({l10n_util::GetStringUTF16(message_id)}, {false});
  }

  void OnModels(RemoteModelClient::Outcome outcome) {
    models_request_.reset();
    std::optional<std::vector<std::u16string>> models;
    if (outcome.ok) {
      models = ParseModelList(outcome.body);
    }
    if (!models) {
      SetFeedback(IDS_UW_AUTO_SETUP_MODELS_ERROR);
      return;
    }
    if (models->empty()) {
      SetFeedback(IDS_UW_AUTO_SETUP_MODELS_EMPTY);
      return;
    }
    // Keep the saved selection first so it stays the initial selection.
    std::vector<std::u16string> items;
    std::vector<bool> enabled;
    if (!saved_model_.empty() &&
        std::ranges::find(*models, saved_model_) != models->end()) {
      items.push_back(saved_model_);
      enabled.push_back(true);
    }
    for (const auto& model : *models) {
      if (!items.empty() && items.front() == model) {
        continue;
      }
      items.push_back(model);
      enabled.push_back(true);
    }
    model_list_->SetItems(std::move(items), std::move(enabled));
  }

  const base::WeakPtr<TabOrganizationController> controller_;
  const raw_ptr<Profile> profile_;
  const ConnectionMode mode_;
  const std::u16string saved_model_;
  raw_ptr<PickListModel> model_list_ = nullptr;
  std::unique_ptr<RemoteModelClient> models_request_;
  base::WeakPtrFactory<AiSetupDelegate> weak_factory_{this};
};

}  // namespace

// static
void AiSetupDialog::Show(views::View* anchor,
                         base::WeakPtr<TabOrganizationController> controller) {
  if (!anchor || !controller) {
    return;
  }
  Profile* profile = controller->profile();
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  const AiConnectionConfig config =
      ReadAiConnectionConfig(profile->GetPrefs());
  const std::u16string saved_model = base::UTF8ToUTF16(config.model);
  auto delegate = std::make_unique<AiSetupDelegate>(controller, profile,
                                                    config.mode, saved_model);
  auto* raw_delegate = delegate.get();

  const bool ollama = config.mode == ConnectionMode::kOllama;
  std::vector<std::u16string> mode_labels = {
      l10n_util::GetStringUTF16(ollama ? IDS_UW_AUTO_SETUP_MODE_OLLAMA
                                       : IDS_UW_AUTO_SETUP_MODE_OPENAI),
      l10n_util::GetStringUTF16(ollama ? IDS_UW_AUTO_SETUP_MODE_OPENAI
                                       : IDS_UW_AUTO_SETUP_MODE_OLLAMA)};
  std::vector<std::u16string> model_labels = {
      saved_model.empty() ? l10n_util::GetStringUTF16(
                                IDS_UW_AUTO_SETUP_MODEL_EMPTY)
                          : saved_model};
  auto model_list = std::make_unique<PickListModel>(
      model_labels, std::vector<bool>{!saved_model.empty()});
  auto* raw_model_list = model_list.get();
  raw_delegate->SetModelList(raw_model_list);

  auto model = ui::DialogModel::Builder(std::move(delegate))
                   .SetTitle(l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_TITLE))
                   .AddCombobox(kModeField,
                                l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_MODE),
                                std::make_unique<PickListModel>(
                                    std::move(mode_labels),
                                    std::vector<bool>{true, true}))
                   .AddTextfield(kEndpointField,
                                 l10n_util::GetStringUTF16(
                                     IDS_UW_AUTO_SETUP_ENDPOINT),
                                 base::UTF8ToUTF16(
                                     config.endpoint.empty()
                                         ? (ollama ? kOllamaDefaultEndpoint
                                                   : "")
                                         : config.endpoint))
                   .AddMenuItem(ui::ImageModel(),
                                l10n_util::GetStringUTF16(
                                    IDS_UW_AUTO_SETUP_LOAD_MODELS),
                                base::BindRepeating(&AiSetupDelegate::LoadModels,
                                                    base::Unretained(raw_delegate)))
                   .AddCombobox(kModelField,
                                l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_MODEL),
                                std::move(model_list))
                   .AddPasswordField(kKeyField,
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_KEY),
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_KEY),
                                     /*incorrect_password_text=*/u"")
                   .AddParagraph(ui::DialogModelLabel(IDS_UW_AUTO_SCOPE))
                   .AddOkButton(base::BindRepeating(&AiSetupDelegate::Save,
                                                   base::Unretained(raw_delegate)))
                   .AddCancelButton(base::DoNothing())
                   .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(model), anchor, views::BubbleBorder::TOP_LEFT);
  views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET)
      ->Show();
}

}  // namespace uw
