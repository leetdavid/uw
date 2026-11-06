// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/page_translator.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/translation/device_translator.h"

namespace uw {

namespace {

// The error numbers uw_translate.js reads: 2 when the engine does not know a
// language, 1 for any other failure.
constexpr int32_t kFailed = 1;
constexpr int32_t kUnsupportedLanguage = 2;

}  // namespace

PageTranslator::PageTranslator(content::WebContents& web_contents)
    : web_contents_(web_contents) {}

PageTranslator::~PageTranslator() = default;

// static
PageTranslator* PageTranslator::FromWebContents(
    content::WebContents* web_contents) {
  ChromeTranslateClient* client =
      web_contents ? ChromeTranslateClient::FromWebContents(web_contents)
                   : nullptr;
  return client ? client->page_translator() : nullptr;
}

void PageTranslator::UseEngineOnce(TranslationEngine engine) {
  once_ = engine;
}

void PageTranslator::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PageTranslator::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PageTranslator::OnTranslationStarted() {
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  engine_ = once_.value_or(GetTranslationEngine(*profile->GetPrefs()));
  once_.reset();
  problem_ = SegmentTranslator::Result();
  downloading_.clear();
  NotifyChanged();
}

void PageTranslator::TranslateSegments(std::vector<std::string> segments,
                                       const std::string& source_language,
                                       const std::string& target_language,
                                       TranslateSegmentsCallback callback) {
  SegmentTranslator* translator = TranslatorFor(engine_);
  if (!translator) {
    OnTranslated(std::move(callback),
                 SegmentTranslator::Result::Failed(std::string()));
    return;
  }
  translator->Translate(
      std::move(segments), source_language, target_language,
      base::BindOnce(&PageTranslator::OnTranslated, weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

SegmentTranslator* PageTranslator::TranslatorFor(TranslationEngine engine) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  if (!IsTranslationEngineSetUp(profile, engine)) {
    return nullptr;
  }
  switch (engine) {
    case TranslationEngine::kDevice:
      if (!device_) {
        device_ = std::make_unique<DeviceTranslator>(
            profile, base::BindRepeating(&PageTranslator::OnDownload,
                                         weak_factory_.GetWeakPtr()));
      }
      return device_.get();
    case TranslationEngine::kNone:
    case TranslationEngine::kGoogleCloud:
    case TranslationEngine::kAi:
      return nullptr;
  }
}

void PageTranslator::OnTranslated(TranslateSegmentsCallback callback,
                                  SegmentTranslator::Result result) {
  if (result.problem == SegmentTranslator::Result::Problem::kNone) {
    std::move(callback).Run(std::move(result.translations), 0);
    return;
  }
  const bool unsupported =
      result.problem ==
      SegmentTranslator::Result::Problem::kUnsupportedLanguage;
  problem_ = std::move(result);
  downloading_.clear();
  NotifyChanged();
  std::move(callback).Run(std::nullopt,
                          unsupported ? kUnsupportedLanguage : kFailed);
}

void PageTranslator::OnDownload(const std::string& language, int percent) {
  downloading_ = percent < 100 ? language : std::string();
  download_percent_ = percent;
  NotifyChanged();
}

void PageTranslator::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnPageTranslatorChanged();
  }
}

}  // namespace uw
