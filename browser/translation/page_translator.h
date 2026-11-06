// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_PAGE_TRANSLATOR_H_
#define UW_BROWSER_TRANSLATION_PAGE_TRANSLATOR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "components/translate/content/browser/content_translate_driver.h"
#include "uw/browser/translation/segment_translator.h"
#include "uw/browser/translation/translation_engine.h"

namespace content {
class WebContents;
}

namespace uw {

// Translates a tab's page text with the profile's translation engine, or the
// engine picked under "Translate with" for one translation. Chromium's
// translate client owns one per tab. See docs/plans/page-translation.md.
class PageTranslator
    : public translate::ContentTranslateDriver::TextTranslator {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The engine, the problem, or a download changed.
    virtual void OnPageTranslatorChanged() = 0;
    // The tab is going, and the translator with it.
    virtual void OnPageTranslatorDestroyed() = 0;
  };

  explicit PageTranslator(content::WebContents& web_contents);
  ~PageTranslator() override;

  static PageTranslator* FromWebContents(content::WebContents* web_contents);

  // Translates the page's next translation with `engine`, that one time.
  void UseEngineOnce(TranslationEngine engine);

  // The engine of the page's current or last translation.
  TranslationEngine engine() const { return engine_; }

  // What stopped the page's last translation, if anything.
  const SegmentTranslator::Result& problem() const { return problem_; }

  // The language this device is downloading for the translation, or empty,
  // and the percentage done.
  const std::string& downloading_language() const { return downloading_; }
  int download_percent() const { return download_percent_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // translate::ContentTranslateDriver::TextTranslator:
  void OnTranslationStarted() override;
  void TranslateSegments(std::vector<std::string> segments,
                         const std::string& source_language,
                         const std::string& target_language,
                         TranslateSegmentsCallback callback) override;

 private:
  SegmentTranslator* TranslatorFor(TranslationEngine engine);
  void OnTranslated(TranslateSegmentsCallback callback,
                    SegmentTranslator::Result result);
  void OnDownload(const std::string& language, int percent);
  void NotifyChanged();

  const raw_ref<content::WebContents> web_contents_;
  std::optional<TranslationEngine> once_;
  TranslationEngine engine_ = TranslationEngine::kNone;
  SegmentTranslator::Result problem_;
  std::string downloading_;
  int download_percent_ = 0;
  std::unique_ptr<SegmentTranslator> device_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<PageTranslator> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_PAGE_TRANSLATOR_H_
