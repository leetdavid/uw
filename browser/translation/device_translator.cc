// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/device_translator.h"

#include <algorithm>
#include <utility>

#include "base/barrier_callback.h"
#include "base/containers/fixed_flat_map.h"
#include "base/functional/bind.h"
#include "chrome/browser/on_device_translation/service_controller_manager_factory.h"
#include "components/on_device_translation/public/language_pack.h"
#include "components/on_device_translation/public/supported_languages.h"
#include "components/on_device_translation/service_controller_manager.h"
#include "components/on_device_translation/translation_manager_util.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace uw {

namespace {

using on_device_translation::LanguagePackKey;
using on_device_translation::OnDeviceTranslationController;
using on_device_translation::OnDeviceTranslationInstaller;
using CanTranslateResult = OnDeviceTranslationController::CanTranslateResult;
using CreateTranslatorError =
    OnDeviceTranslationController::CreateTranslatorError;

// The engine keeps a service per origin; every page shares this one.
url::Origin ServiceOrigin() {
  return url::Origin::Create(GURL("chrome://translate/"));
}

std::string LanguageOfPack(LanguagePackKey pack) {
  return std::string(on_device_translation::ToLanguageCode(
      on_device_translation::NonEnglishSupportedLanguageFromLanguagePackKey(
          pack)));
}

}  // namespace

DeviceTranslator::Request::Request(std::vector<std::string> segments,
                                   ResultCallback callback)
    : segments(std::move(segments)), callback(std::move(callback)) {}
DeviceTranslator::Request::Request(Request&&) = default;
DeviceTranslator::Request& DeviceTranslator::Request::operator=(Request&&) =
    default;
DeviceTranslator::Request::~Request() = default;

DeviceTranslator::DeviceTranslator(content::BrowserContext* context,
                                   DownloadCallback on_download)
    : context_(context), on_download_(std::move(on_download)) {}

DeviceTranslator::~DeviceTranslator() = default;

// static
std::optional<std::string> DeviceTranslator::LanguageCode(
    std::string_view language) {
  static constexpr auto kAliases =
      base::MakeFixedFlatMap<std::string_view, std::string_view>({
          {"he", "iw"},
          {"nb", "no"},
          {"zh-HK", "zh-Hant"},
          {"zh-TW", "zh-Hant"},
      });
  if (auto alias = kAliases.find(language); alias != kAliases.end()) {
    language = alias->second;
  }
  return on_device_translation::LookupMatchingLocaleByBestFit(
      on_device_translation::kSupportedLanguageCodes, language);
}

void DeviceTranslator::Translate(std::vector<std::string> segments,
                                 const std::string& source_language,
                                 const std::string& target_language,
                                 ResultCallback callback) {
  std::optional<std::string> source = LanguageCode(source_language);
  if (!source) {
    std::move(callback).Run(Result::Unsupported(source_language));
    return;
  }
  std::optional<std::string> target = LanguageCode(target_language);
  if (!target) {
    std::move(callback).Run(Result::Unsupported(target_language));
    return;
  }
  Request request(std::move(segments), std::move(callback));
  if (*source == *target) {
    Result result;
    result.translations = std::move(request.segments);
    std::move(request.callback).Run(std::move(result));
    return;
  }
  request.source = *source;
  request.target = *target;
  Queue(std::move(request));
}

void DeviceTranslator::Queue(Request request) {
  if (request.source == source_ && request.target == target_) {
    if (translator_.is_bound()) {
      Run(std::move(request));
      return;
    }
    if (creating_) {
      waiting_.push_back(std::move(request));
      return;
    }
  }
  FailAll(Result::Failed(std::string()));
  weak_factory_.InvalidateWeakPtrs();
  installer_observation_.Reset();
  downloading_.clear();
  translator_.reset();
  source_ = request.source;
  target_ = request.target;
  waiting_.push_back(std::move(request));
  auto* manager =
      on_device_translation::ServiceControllerManagerFactory::GetInstance()
          ->Get(context_);
  if (!manager) {
    FailAll(Result::Failed(std::string()));
    return;
  }
  creating_ = true;
  manager->CanTranslate(ServiceOrigin(), source_, target_,
                        base::BindOnce(&DeviceTranslator::OnCanTranslate,
                                       weak_factory_.GetWeakPtr()));
}

void DeviceTranslator::OnCanTranslate(CanTranslateResult result) {
  switch (result) {
    case CanTranslateResult::kNoNotSupportedLanguage:
      FailAll(Result::Unsupported(source_ == "en" ? target_ : source_));
      return;
    case CanTranslateResult::kNoServiceCrashed:
    case CanTranslateResult::kNoExceedsServiceCountLimitation:
      FailAll(Result::Failed(std::string()));
      return;
    case CanTranslateResult::kAfterDownloadLibraryNotReady:
    case CanTranslateResult::kAfterDownloadLanguagePackNotReady:
    case CanTranslateResult::kAfterDownloadLibraryAndLanguagePackNotReady: {
      OnDeviceTranslationInstaller* installer =
          OnDeviceTranslationInstaller::GetInstance();
      std::set<LanguagePackKey> installed = installer->InstalledLanguagePacks();
      for (LanguagePackKey pack :
           on_device_translation::CalculateRequiredLanguagePacks(source_,
                                                                 target_)) {
        if (!installed.contains(pack)) {
          downloading_.insert(pack);
          on_download_.Run(LanguageOfPack(pack), 0);
        }
      }
      if (!downloading_.empty() && !installer_observation_.IsObserving()) {
        installer_observation_.Observe(installer);
      }
      break;
    }
    case CanTranslateResult::kReadily:
      break;
  }
  auto* manager =
      on_device_translation::ServiceControllerManagerFactory::GetInstance()
          ->Get(context_);
  manager->CreateTranslator(
      ServiceOrigin(), source_, target_,
      base::BindOnce(&DeviceTranslator::OnTranslatorCreated,
                     weak_factory_.GetWeakPtr()));
}

void DeviceTranslator::OnTranslatorCreated(
    base::expected<
        mojo::PendingRemote<on_device_translation::mojom::OnDeviceTranslator>,
        CreateTranslatorError> result) {
  creating_ = false;
  installer_observation_.Reset();
  downloading_.clear();
  if (!result.has_value() &&
      result.error() == CreateTranslatorError::kServiceCrashed) {
    std::vector<Request> waiting = std::move(waiting_);
    for (Request& request : waiting) {
      if (request.retries-- > 0) {
        Queue(std::move(request));
      } else {
        std::move(request.callback).Run(Result::Failed(std::string()));
      }
    }
    return;
  }
  if (!result.has_value()) {
    FailAll(result.error() == CreateTranslatorError::kNotSupportedLanguage
                ? Result::Unsupported(source_ == "en" ? target_ : source_)
                : Result::Failed(std::string()));
    return;
  }
  translator_.Bind(std::move(result.value()));
  translator_.set_disconnect_handler(base::BindOnce(
      &DeviceTranslator::OnDisconnect, weak_factory_.GetWeakPtr()));
  std::vector<Request> waiting = std::move(waiting_);
  for (Request& request : waiting) {
    Run(std::move(request));
  }
}

void DeviceTranslator::Run(Request request) {
  if (request.segments.empty()) {
    std::move(request.callback).Run(Result());
    return;
  }
  const std::vector<std::string> segments = request.segments;
  auto done = base::BarrierCallback<Translated>(
      segments.size(),
      base::BindOnce(&DeviceTranslator::OnRun, run_weak_factory_.GetWeakPtr(),
                     std::move(request), lost_));
  for (size_t index = 0; index < segments.size(); ++index) {
    translator_->Translate(
        segments[index],
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(
                [](size_t index, base::RepeatingCallback<void(Translated)> done,
                   const std::optional<std::string>& text) {
                  done.Run(Translated(index, text));
                },
                index, done),
            std::nullopt));
  }
}

void DeviceTranslator::OnRun(Request request,
                             int lost,
                             std::vector<Translated> translated) {
  const bool cut_off =
      lost != lost_ && !std::ranges::all_of(translated, [](const auto& item) {
        return item.second.has_value();
      });
  if (cut_off && request.retries-- > 0) {
    Queue(std::move(request));
    return;
  }
  Result result;
  result.translations = std::move(request.segments);
  bool any = false;
  for (auto& [index, text] : translated) {
    if (text) {
      result.translations[index] = std::move(*text);
      any = true;
    }
  }
  std::move(request.callback)
      .Run(any ? std::move(result) : Result::Failed(std::string()));
}

void DeviceTranslator::OnDisconnect() {
  translator_.reset();
  ++lost_;
}

void DeviceTranslator::FailAll(Result result) {
  std::vector<Request> waiting = std::move(waiting_);
  for (Request& request : waiting) {
    std::move(request.callback).Run(result);
  }
}

void DeviceTranslator::OnLanguagePackInstalled(LanguagePackKey language_pack) {
  if (downloading_.erase(language_pack)) {
    on_download_.Run(LanguageOfPack(language_pack), 100);
  }
}

void DeviceTranslator::OnLanguagePackInstallationChanged(
    LanguagePackKey language_pack) {}

void DeviceTranslator::OnInstallationChanged() {}

void DeviceTranslator::OnLanguagePackProgress(LanguagePackKey language_pack,
                                              int progress) {
  if (downloading_.contains(language_pack)) {
    on_download_.Run(LanguageOfPack(language_pack), progress);
  }
}

}  // namespace uw
