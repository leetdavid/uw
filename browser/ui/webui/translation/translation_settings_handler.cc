// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/translation/translation_settings_handler.h"

#include <algorithm>

#include "base/byte_size.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/on_device_translation/public/language_pack.h"
#include "components/on_device_translation/public/supported_languages.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/model_lister.h"
#include "uw/browser/translation/ai_translator.h"
#include "uw/browser/translation/device_translator.h"
#include "uw/browser/translation/google_cloud_translator.h"
#include "uw/browser/translation/translation_prefs.h"

namespace uw {

namespace {

using on_device_translation::LanguagePackKey;
using on_device_translation::OnDeviceTranslationInstaller;

std::string LanguageOfPack(LanguagePackKey pack) {
  return std::string(on_device_translation::ToLanguageCode(
      on_device_translation::NonEnglishSupportedLanguageFromLanguagePackKey(
          pack)));
}

std::u16string LanguageName(const std::string& code) {
  return l10n_util::GetDisplayNameForLocale(
      code, g_browser_process->GetApplicationLocale(), true);
}

}  // namespace

TranslationSettingsHandler::ModelsRequest::ModelsRequest() = default;
TranslationSettingsHandler::ModelsRequest::~ModelsRequest() = default;

TranslationSettingsHandler::TranslationSettingsHandler() = default;

TranslationSettingsHandler::~TranslationSettingsHandler() = default;

void TranslationSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "uwGetTranslationLanguages",
      base::BindRepeating(&TranslationSettingsHandler::HandleGetLanguages,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwRemoveTranslationLanguage",
      base::BindRepeating(&TranslationSettingsHandler::HandleRemoveLanguage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwGetGoogleCloudKey",
      base::BindRepeating(&TranslationSettingsHandler::HandleGetGoogleCloudKey,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwSaveGoogleCloudKey",
      base::BindRepeating(&TranslationSettingsHandler::HandleSaveGoogleCloudKey,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwRemoveGoogleCloudKey",
      base::BindRepeating(
          &TranslationSettingsHandler::HandleRemoveGoogleCloudKey,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwGetTranslationModels",
      base::BindRepeating(&TranslationSettingsHandler::HandleGetModels,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "uwSetTranslationModel",
      base::BindRepeating(&TranslationSettingsHandler::HandleSetModel,
                          base::Unretained(this)));
}

void TranslationSettingsHandler::OnJavascriptAllowed() {
  if (auto* installer = OnDeviceTranslationInstaller::GetInstance()) {
    observation_.Observe(installer);
  }
}

void TranslationSettingsHandler::OnJavascriptDisallowed() {
  observation_.Reset();
  weak_factory_.InvalidateWeakPtrs();
}

void TranslationSettingsHandler::HandleGetLanguages(
    const base::ListValue& args) {
  AllowJavascript();
  const std::string callback_id = args[0].GetString();
  const std::string target = args.size() > 1 && args[1].is_string()
                                 ? args[1].GetString()
                                 : std::string();
  std::vector<std::pair<std::string, base::FilePath>> paths;
  if (auto* installer = OnDeviceTranslationInstaller::GetInstance()) {
    for (LanguagePackKey pack : installer->InstalledLanguagePacks()) {
      paths.emplace_back(LanguageOfPack(pack),
                         installer->GetLanguagePackPath(pack));
    }
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](std::vector<std::pair<std::string, base::FilePath>> paths) {
            Sizes sizes;
            for (const auto& [code, path] : paths) {
              sizes.emplace_back(code, base::ComputeDirectorySize(path));
            }
            return sizes;
          },
          std::move(paths)),
      base::BindOnce(&TranslationSettingsHandler::OnSizes,
                     weak_factory_.GetWeakPtr(), callback_id, target));
}

void TranslationSettingsHandler::OnSizes(const std::string& callback_id,
                                         const std::string& target,
                                         Sizes sizes) {
  std::ranges::sort(sizes, [](const auto& a, const auto& b) {
    return LanguageName(a.first) < LanguageName(b.first);
  });
  base::ListValue languages;
  for (const auto& [code, bytes] : sizes) {
    languages.Append(
        base::DictValue()
            .Set("code", code)
            .Set("name", LanguageName(code))
            .Set("size", ui::FormatBytes(base::ByteSize(static_cast<uint64_t>(
                             std::max<int64_t>(bytes, 0))))));
  }
  ResolveJavascriptCallback(
      base::Value(callback_id),
      base::DictValue()
          .Set("languages", std::move(languages))
          .Set("targetSupported",
               target.empty() || DeviceTranslator::LanguageCode(target))
          .Set("targetName", LanguageName(target)));
}

void TranslationSettingsHandler::HandleRemoveLanguage(
    const base::ListValue& args) {
  auto* installer = OnDeviceTranslationInstaller::GetInstance();
  if (!installer || args.empty() || !args[0].is_string()) {
    return;
  }
  for (LanguagePackKey pack : installer->InstalledLanguagePacks()) {
    if (LanguageOfPack(pack) == args[0].GetString()) {
      installer->UnInstallLanguagePack(pack);
    }
  }
  NotifyChanged();
}

void TranslationSettingsHandler::HandleGetGoogleCloudKey(
    const base::ListValue& args) {
  AllowJavascript();
  ResolveJavascriptCallback(
      args[0], base::Value(g_browser_process->local_state()->GetBoolean(
                   kTranslationGoogleCloudKeySavedPref)));
}

void TranslationSettingsHandler::HandleSaveGoogleCloudKey(
    const base::ListValue& args) {
  AllowJavascript();
  const std::string callback_id = args[0].GetString();
  const std::string key = args.size() > 1 && args[1].is_string()
                              ? std::string(base::TrimWhitespaceASCII(
                                    args[1].GetString(), base::TRIM_ALL))
                              : std::string();
  if (key.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::DictValue().Set("ok", false));
    return;
  }
  GoogleCloudTranslator::CheckKey(
      Profile::FromWebUI(web_ui())
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      key,
      base::BindOnce(&TranslationSettingsHandler::OnKeyChecked,
                     weak_factory_.GetWeakPtr(), callback_id, key));
}

void TranslationSettingsHandler::OnKeyChecked(
    const std::string& callback_id,
    const std::string& key,
    std::optional<std::string> problem) {
  const bool ok =
      !problem && ApiKeyStore().SetKey(GoogleCloudTranslator::kKeyAccount, key);
  if (ok) {
    g_browser_process->local_state()->SetBoolean(
        kTranslationGoogleCloudKeySavedPref, true);
  }
  ResolveJavascriptCallback(base::Value(callback_id),
                            base::DictValue().Set("ok", ok).Set(
                                "message", problem.value_or(std::string())));
}

void TranslationSettingsHandler::HandleRemoveGoogleCloudKey(
    const base::ListValue& args) {
  ApiKeyStore().DeleteKey(GoogleCloudTranslator::kKeyAccount);
  g_browser_process->local_state()->SetBoolean(
      kTranslationGoogleCloudKeySavedPref, false);
}

void TranslationSettingsHandler::HandleGetModels(const base::ListValue& args) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui())->GetOriginalProfile();
  const size_t id = next_models_request_++;
  auto request = std::make_unique<ModelsRequest>();
  request->callback_id = args[0].GetString();
  ModelsRequest* raw = request.get();
  models_requests_[id] = std::move(request);
  std::vector<Provider> providers;
  for (const Provider& provider : ReadProviders(*profile->GetPrefs())) {
    if (LanguageFormatOf(provider)) {
      providers.push_back(provider);
    }
  }
  raw->waiting = providers.size();
  if (providers.empty()) {
    AnswerModels(id);
    return;
  }
  for (const Provider& provider : providers) {
    std::string key;
    if (!provider.key_account.empty()) {
      key = ApiKeyStore().GetKey(provider.key_account).value_or(std::string());
    }
    auto lister = std::make_unique<ModelLister>(profile);
    lister->List(provider, std::move(key),
                 base::BindOnce(&TranslationSettingsHandler::OnListing,
                                weak_factory_.GetWeakPtr(), id, provider));
    raw->listers.push_back(std::move(lister));
  }
}

void TranslationSettingsHandler::OnListing(size_t id,
                                           const Provider& provider,
                                           ModelListing listing) {
  auto request = models_requests_.find(id);
  if (request == models_requests_.end()) {
    return;
  }
  for (const ListedModel& model : listing.models) {
    if (!model.writes_text) {
      continue;
    }
    ModelChoice choice;
    choice.provider_id = provider.id;
    choice.model = model.name;
    choice.format = model.format;
    if (!model.abilities.efforts.empty()) {
      choice.effort = model.abilities.efforts.front();
    }
    listed_[{provider.id, model.name}] = choice;
    request->second->models.Append(
        base::DictValue()
            .Set("provider", provider.id)
            .Set("model", model.name)
            .Set("label", model.name + " · " + provider.name));
  }
  if (--request->second->waiting == 0) {
    // The lister is still on the stack.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TranslationSettingsHandler::AnswerModels,
                                  weak_factory_.GetWeakPtr(), id));
  }
}

void TranslationSettingsHandler::AnswerModels(size_t id) {
  auto request = models_requests_.find(id);
  if (request == models_requests_.end()) {
    return;
  }
  const PrefService& prefs =
      *Profile::FromWebUI(web_ui())->GetOriginalProfile()->GetPrefs();
  std::optional<ModelChoice> picked = ReadTranslationModel(prefs);
  base::DictValue answer;
  answer.Set("models", std::move(request->second->models));
  answer.Set("provider", picked ? picked->provider_id : std::string());
  answer.Set("model", picked ? picked->model : std::string());
  const std::string callback_id = request->second->callback_id;
  models_requests_.erase(request);
  ResolveJavascriptCallback(base::Value(callback_id), answer);
}

void TranslationSettingsHandler::HandleSetModel(const base::ListValue& args) {
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  ModelChoice choice;
  auto listed = listed_.find({args[0].GetString(), args[1].GetString()});
  if (listed != listed_.end()) {
    choice = listed->second;
  } else {
    choice.provider_id = args[0].GetString();
    choice.model = args[1].GetString();
  }
  SetTranslationModel(
      *Profile::FromWebUI(web_ui())->GetOriginalProfile()->GetPrefs(), choice);
}

void TranslationSettingsHandler::NotifyChanged() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("uw-translation-languages-changed");
  }
}

void TranslationSettingsHandler::OnLanguagePackInstalled(
    LanguagePackKey language_pack) {
  NotifyChanged();
}

void TranslationSettingsHandler::OnLanguagePackInstallationChanged(
    LanguagePackKey language_pack) {
  NotifyChanged();
}

void TranslationSettingsHandler::OnInstallationChanged() {
  NotifyChanged();
}

}  // namespace uw
