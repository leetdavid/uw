// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/translation/translation_bubble.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/view.h"
#include "uw/browser/translation/page_translator.h"
#include "uw/browser/translation/translate_menu.h"
#include "uw/browser/translation/translation_engine.h"

namespace uw {

namespace {

std::u16string LanguageName(const std::string& code) {
  return l10n_util::GetDisplayNameForLocale(
      code, g_browser_process->GetApplicationLocale(), true);
}

std::u16string EngineName(TranslationEngine engine) {
  switch (engine) {
    case TranslationEngine::kDevice:
    case TranslationEngine::kNone:
      return l10n_util::GetStringUTF16(IDS_UW_TRANSLATION_ENGINE_DEVICE);
    case TranslationEngine::kGoogleCloud:
      return l10n_util::GetStringUTF16(IDS_UW_TRANSLATION_ENGINE_GOOGLE_CLOUD);
    case TranslationEngine::kAi:
      return l10n_util::GetStringUTF16(IDS_UW_TRANSLATION_ENGINE_AI);
  }
}

int CommandOf(TranslationEngine engine) {
  switch (engine) {
    case TranslationEngine::kDevice:
    case TranslationEngine::kNone:
      return kTranslateWithDevice;
    case TranslationEngine::kGoogleCloud:
      return kTranslateWithGoogleCloud;
    case TranslationEngine::kAi:
      return kTranslateWithAi;
  }
}

std::u16string ProblemTitle(TranslationEngine engine,
                            const SegmentTranslator::Result& problem,
                            bool selection) {
  const std::u16string name = EngineName(engine);
  switch (problem.problem) {
    case SegmentTranslator::Result::Problem::kNone:
      return std::u16string();
    case SegmentTranslator::Result::Problem::kUnsupportedLanguage:
      if (problem.language.empty() || problem.language == "auto" ||
          problem.language == "und") {
        return l10n_util::GetStringFUTF16(
            selection ? IDS_UW_TRANSLATE_SELECTION_UNKNOWN_LANGUAGE
                      : IDS_UW_TRANSLATE_ENGINE_UNKNOWN_LANGUAGE,
            name);
      }
      return l10n_util::GetStringFUTF16(IDS_UW_TRANSLATE_ENGINE_LACKS_LANGUAGE,
                                        name, LanguageName(problem.language));
    case SegmentTranslator::Result::Problem::kFailed:
      return l10n_util::GetStringFUTF16(selection
                                            ? IDS_UW_TRANSLATE_SELECTION_FAILED
                                            : IDS_UW_TRANSLATE_ENGINE_FAILED,
                                        name);
  }
}

}  // namespace

std::u16string TranslationDownloadText(content::WebContents* web_contents) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  if (!translator || translator->downloading_language().empty()) {
    return std::u16string();
  }
  return l10n_util::GetStringFUTF16(
      IDS_UW_TRANSLATE_DOWNLOADING,
      LanguageName(translator->downloading_language()),
      base::NumberToString16(translator->download_percent()));
}

std::u16string TranslationProblemTitle(content::WebContents* web_contents) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  return translator ? ProblemTitle(translator->engine(), translator->problem(),
                                   /*selection=*/false)
                    : std::u16string();
}

std::u16string TranslationProblemMessage(content::WebContents* web_contents) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  return translator ? base::UTF8ToUTF16(translator->problem().message)
                    : std::u16string();
}

std::u16string SelectionProblemTitle(content::WebContents* web_contents) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  return translator ? ProblemTitle(translator->selection_engine(),
                                   translator->selection_problem(),
                                   /*selection=*/true)
                    : std::u16string();
}

std::u16string SelectionProblemMessage(content::WebContents* web_contents) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  return translator ? base::UTF8ToUTF16(translator->selection_problem().message)
                    : std::u16string();
}

bool AddOtherEngineButtons(content::WebContents* web_contents,
                           views::View* buttons,
                           base::RepeatingClosure close_bubble) {
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  if (!translator ||
      translator->problem().problem !=
          SegmentTranslator::Result::Problem::kUnsupportedLanguage) {
    return false;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  bool added = false;
  for (TranslationEngine engine :
       {TranslationEngine::kDevice, TranslationEngine::kGoogleCloud,
        TranslationEngine::kAi}) {
    if (engine == translator->engine() ||
        !IsTranslationEngineSetUp(profile, engine)) {
      continue;
    }
    buttons->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ExecuteTranslateMenuCommand,
                            base::Unretained(web_contents), CommandOf(engine),
                            std::u16string()),
        l10n_util::GetStringFUTF16(IDS_UW_TRANSLATE_WITH_ENGINE,
                                   EngineName(engine))));
    added = true;
  }
  if (!added) {
    buttons->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            [](content::WebContents* web_contents,
               base::RepeatingClosure close_bubble) {
              ShowTranslationSettings(web_contents);
              close_bubble.Run();
            },
            base::Unretained(web_contents), close_bubble),
        l10n_util::GetStringUTF16(IDS_UW_TRANSLATE_SET_UP_ANOTHER)));
  }
  return true;
}

}  // namespace uw
