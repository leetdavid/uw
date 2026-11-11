// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/page_translator.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/translation/ai_translator.h"
#include "uw/browser/translation/device_translator.h"
#include "uw/browser/translation/google_cloud_translator.h"

namespace uw {

namespace {

// The error numbers uw_translate.js reads: 2 when the engine does not know a
// language, 1 for any other failure.
constexpr int32_t kFailed = 1;
constexpr int32_t kUnsupportedLanguage = 2;

bool IsBlank(const std::string& line) {
  return base::TrimWhitespaceASCII(line, base::TRIM_ALL).empty();
}

}  // namespace

PageTranslator::PageTranslator(content::WebContents& web_contents)
    : web_contents_(web_contents) {}

PageTranslator::~PageTranslator() {
  for (Observer& observer : observers_) {
    observer.OnPageTranslatorDestroyed();
  }
}

// static
PageTranslator* PageTranslator::FromWebContents(
    content::WebContents* web_contents) {
  ChromeTranslateClient* client =
      web_contents ? ChromeTranslateClient::FromWebContents(web_contents)
                   : nullptr;
  return client ? client->page_translator() : nullptr;
}

// static
void PageTranslator::TranslateSelectionIn(
    content::WebContents* web_contents,
    const PartialTranslateRequest& request,
    PartialTranslateManager::PartialTranslateCallback callback) {
  PageTranslator* translator = FromWebContents(web_contents);
  if (!translator) {
    std::move(callback).Run(PartialTranslateResponse());
    return;
  }
  translator->TranslateSelection(request, std::move(callback));
}

void PageTranslator::UseEngineOnce(TranslationEngine engine) {
  once_ = engine;
}

void PageTranslator::UseSelectionEngineOnce(TranslationEngine engine) {
  selection_once_ = engine;
}

void PageTranslator::OnSelectionTranslationStarted() {
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  selection_engine_ =
      selection_once_.value_or(GetTranslationEngine(*profile->GetPrefs()));
  selection_once_.reset();
}

void PageTranslator::TranslateSelection(
    const PartialTranslateRequest& request,
    PartialTranslateManager::PartialTranslateCallback callback) {
  selection_problem_ = SegmentTranslator::Result();
  // The bubble's first request carries the page's language as a hint. This
  // device needs a language; the others tell it from the text.
  std::string source = request.source_language.value_or(std::string());
  if (request.apply_lang_hint &&
      selection_engine_ != TranslationEngine::kDevice) {
    source.clear();
  }
  std::vector<std::string> lines =
      base::SplitString(base::UTF16ToUTF8(request.selection_text), "\n",
                        base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  std::vector<std::string> segments;
  for (const std::string& line : lines) {
    if (!IsBlank(line)) {
      segments.push_back(base::EscapeForHTML(line));
    }
  }
  SegmentTranslator* translator =
      TranslatorFor(selection_engine_, /*selection=*/true);
  if (!translator) {
    OnSelectionTranslated(std::move(lines), source, request.target_language,
                          std::move(callback),
                          SegmentTranslator::Result::Failed(std::string()));
    return;
  }
  translator->Translate(
      std::move(segments), source, request.target_language,
      base::BindOnce(&PageTranslator::OnSelectionTranslated,
                     weak_factory_.GetWeakPtr(), std::move(lines), source,
                     request.target_language, std::move(callback)));
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
  SegmentTranslator* translator = TranslatorFor(engine_, /*selection=*/false);
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

SegmentTranslator* PageTranslator::TranslatorFor(TranslationEngine engine,
                                                 bool selection) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  if (!IsTranslationEngineSetUp(profile, engine)) {
    return nullptr;
  }
  switch (engine) {
    case TranslationEngine::kDevice:
      if (selection) {
        if (!selection_device_) {
          selection_device_ =
              std::make_unique<DeviceTranslator>(profile, base::DoNothing());
        }
        return selection_device_.get();
      }
      if (!device_) {
        device_ = std::make_unique<DeviceTranslator>(
            profile, base::BindRepeating(&PageTranslator::OnDownload,
                                         weak_factory_.GetWeakPtr()));
      }
      return device_.get();
    case TranslationEngine::kGoogleCloud:
      if (!google_cloud_) {
        google_cloud_ = std::make_unique<GoogleCloudTranslator>(
            profile->GetDefaultStoragePartition()
                ->GetURLLoaderFactoryForBrowserProcess());
      }
      return google_cloud_.get();
    case TranslationEngine::kAi:
      if (!ai_) {
        ai_ = std::make_unique<AiTranslator>(profile);
      }
      return ai_.get();
    case TranslationEngine::kNone:
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

void PageTranslator::OnSelectionTranslated(
    std::vector<std::string> lines,
    std::string source_language,
    std::string target_language,
    PartialTranslateManager::PartialTranslateCallback callback,
    SegmentTranslator::Result result) {
  PartialTranslateResponse response;
  if (result.problem != SegmentTranslator::Result::Problem::kNone) {
    selection_problem_ = std::move(result);
    std::move(callback).Run(response);
    return;
  }
  std::vector<std::u16string> translated;
  auto translation = result.translations.begin();
  for (const std::string& line : lines) {
    if (IsBlank(line) || translation == result.translations.end()) {
      translated.push_back(base::UTF8ToUTF16(line));
    } else {
      translated.push_back(
          base::UnescapeForHTML(base::UTF8ToUTF16(*translation++)));
    }
  }
  response.status = PartialTranslateStatus::kSuccess;
  response.translated_text = base::JoinString(translated, u"\n");
  response.source_language =
      source_language.empty() ? "und" : std::move(source_language);
  response.target_language = std::move(target_language);
  std::move(callback).Run(response);
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
