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
#include "components/translate/content/browser/partial_translate_manager.h"
#include "uw/browser/translation/segment_translator.h"
#include "uw/browser/translation/translation_engine.h"

namespace content {
class WebContents;
}

namespace uw {

// Translates a tab's page text, or the text selected in it, with the
// profile's translation engine, or the engine picked under "Translate with"
// for one translation. Chromium's translate client owns one per tab. See
// docs/plans/page-translation.md.
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

  // The partial translate manager's translator: the tab's page translator
  // translates the selection.
  static void TranslateSelectionIn(
      content::WebContents* web_contents,
      const PartialTranslateRequest& request,
      PartialTranslateManager::PartialTranslateCallback callback);

  // Translates the page's next translation with `engine`, that one time.
  void UseEngineOnce(TranslationEngine engine);

  // The engine of the page's current or last translation.
  TranslationEngine engine() const { return engine_; }

  // What stopped the page's last translation, if anything.
  const SegmentTranslator::Result& problem() const { return problem_; }

  // Translates the next selection with `engine`, that one time.
  void UseSelectionEngineOnce(TranslationEngine engine);

  // A selection's translate bubble opened: picks the engine for it.
  void OnSelectionTranslationStarted();

  // The engine of the selection's translate bubble.
  TranslationEngine selection_engine() const { return selection_engine_; }

  // What stopped the selection's last translation, if anything.
  const SegmentTranslator::Result& selection_problem() const {
    return selection_problem_;
  }

  // Translates the selection in `request` a line at a time, for Chromium's
  // partial translate bubble.
  void TranslateSelection(
      const PartialTranslateRequest& request,
      PartialTranslateManager::PartialTranslateCallback callback);

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
  SegmentTranslator* TranslatorFor(TranslationEngine engine, bool selection);
  void OnTranslated(TranslateSegmentsCallback callback,
                    SegmentTranslator::Result result);
  void OnSelectionTranslated(
      std::vector<std::string> lines,
      std::string source_language,
      std::string target_language,
      PartialTranslateManager::PartialTranslateCallback callback,
      SegmentTranslator::Result result);
  void OnDownload(const std::string& language, int percent);
  void NotifyChanged();

  const raw_ref<content::WebContents> web_contents_;
  std::optional<TranslationEngine> once_;
  TranslationEngine engine_ = TranslationEngine::kNone;
  SegmentTranslator::Result problem_;
  std::string downloading_;
  int download_percent_ = 0;
  std::optional<TranslationEngine> selection_once_;
  TranslationEngine selection_engine_ = TranslationEngine::kNone;
  SegmentTranslator::Result selection_problem_;
  std::unique_ptr<SegmentTranslator> device_;
  // This device translates one pair of languages at a time, so a selection
  // has its own.
  std::unique_ptr<SegmentTranslator> selection_device_;
  std::unique_ptr<SegmentTranslator> google_cloud_;
  std::unique_ptr<SegmentTranslator> ai_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<PageTranslator> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_PAGE_TRANSLATOR_H_
