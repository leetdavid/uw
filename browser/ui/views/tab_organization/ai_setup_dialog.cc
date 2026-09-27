// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/ai_setup_dialog.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/view.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {
namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kJevEndpointField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kJevKeyField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kNamingEndpointField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kNamingModelField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kNamingKeyField);

// Fixed Keychain account slots; the slots (not the secrets) are unremarkable.
constexpr char kJevAccount[] = "jev";
constexpr char kNamingAccount[] = "naming";

class AiSetupDelegate : public ui::DialogModelDelegate {
 public:
  AiSetupDelegate(base::WeakPtr<TabOrganizationController> controller,
                  Profile* profile)
      : controller_(controller), profile_(profile) {}

  void Save() {
    if (!controller_ || !profile_) {
      return;
    }
    const std::string jev_endpoint = base::UTF16ToUTF8(
        dialog_model()->GetTextfieldByUniqueId(kJevEndpointField)->text());
    const std::string jev_key = base::UTF16ToUTF8(
        dialog_model()->GetPasswordFieldByUniqueId(kJevKeyField)->text());
    const std::string naming_endpoint = base::UTF16ToUTF8(
        dialog_model()->GetTextfieldByUniqueId(kNamingEndpointField)->text());
    const std::string naming_model = base::UTF16ToUTF8(
        dialog_model()->GetTextfieldByUniqueId(kNamingModelField)->text());
    const std::string naming_key = base::UTF16ToUTF8(
        dialog_model()->GetPasswordFieldByUniqueId(kNamingKeyField)->text());
    if (!IsValidProviderEndpoint(jev_endpoint) ||
        !IsValidProviderEndpoint(naming_endpoint) || naming_model.empty() ||
        jev_key.empty() || naming_key.empty()) {
      return;
    }
    PrefService* prefs = profile_->GetPrefs();
    prefs->SetString(kJevEndpointPref, jev_endpoint);
    prefs->SetString(kJevKeyAccountPref, kJevAccount);
    prefs->SetString(kNamingEndpointPref, naming_endpoint);
    prefs->SetString(kNamingModelPref, naming_model);
    prefs->SetString(kNamingKeyAccountPref, kNamingAccount);
    ApiKeyStore keys;
    if (!keys.SetKey(kJevAccount, jev_key) ||
        !keys.SetKey(kNamingAccount, naming_key)) {
      return;
    }
    controller_->ValidateProviders();
  }

 private:
  base::WeakPtr<TabOrganizationController> controller_;
  raw_ptr<Profile> profile_;
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
  auto delegate =
      std::make_unique<AiSetupDelegate>(controller, profile);
  auto* raw_delegate = delegate.get();
  auto model = ui::DialogModel::Builder(std::move(delegate))
                   .SetTitle(l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_TITLE))
                   .AddTextfield(kJevEndpointField,
                                 l10n_util::GetStringUTF16(
                                     IDS_UW_AUTO_SETUP_JEV_ENDPOINT),
                                 base::UTF8ToUTF16(config.jev_endpoint.empty()
                                                       ? kJevDefaultEndpoint
                                                       : config.jev_endpoint))
                   .AddPasswordField(kJevKeyField,
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_JEV_KEY),
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_JEV_KEY),
                                     /*incorrect_password_text=*/u"")
                   .AddTextfield(kNamingEndpointField,
                                 l10n_util::GetStringUTF16(
                                     IDS_UW_AUTO_SETUP_NAMING_ENDPOINT),
                                 base::UTF8ToUTF16(config.naming_endpoint))
                   .AddTextfield(kNamingModelField,
                                 l10n_util::GetStringUTF16(
                                     IDS_UW_AUTO_SETUP_NAMING_MODEL),
                                 base::UTF8ToUTF16(config.naming_model))
                   .AddPasswordField(kNamingKeyField,
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_NAMING_KEY),
                                     l10n_util::GetStringUTF16(
                                         IDS_UW_AUTO_SETUP_NAMING_KEY),
                                     /*incorrect_password_text=*/u"")
                   .AddOkButton(base::BindOnce(&AiSetupDelegate::Save,
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
