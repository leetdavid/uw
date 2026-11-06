// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/translation/translation_settings_handler.h"

#include <algorithm>

#include "base/byte_size.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "components/on_device_translation/public/language_pack.h"
#include "components/on_device_translation/public/supported_languages.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"
#include "uw/browser/translation/device_translator.h"

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
