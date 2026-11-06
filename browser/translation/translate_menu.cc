// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/translate_menu.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/translate/translate_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/translation/page_translator.h"
#include "uw/browser/translation/translation_engine.h"

namespace uw {

namespace {

TranslationEngine EngineOf(int command_id) {
  switch (command_id) {
    case kTranslateWithDevice:
    case kTranslateSelectionWithDevice:
      return TranslationEngine::kDevice;
    case kTranslateWithGoogleCloud:
    case kTranslateSelectionWithGoogleCloud:
      return TranslationEngine::kGoogleCloud;
    case kTranslateWithAi:
    case kTranslateSelectionWithAi:
      return TranslationEngine::kAi;
    default:
      return TranslationEngine::kNone;
  }
}

bool IsSelectionCommand(int command_id) {
  return command_id >= kTranslateSelectionWithDevice &&
         command_id <= kTranslateSelectionWith;
}

Profile* ProfileOf(content::WebContents* web_contents) {
  return Profile::FromBrowserContext(web_contents->GetBrowserContext());
}

translate::TranslateManager* ManagerOf(content::WebContents* web_contents) {
  ChromeTranslateClient* client =
      ChromeTranslateClient::FromWebContents(web_contents);
  return client ? client->GetTranslateManager() : nullptr;
}

}  // namespace

bool IsTranslateMenuCommand(int command_id) {
  return command_id >= kSetUpTranslation &&
         command_id <= kTranslateSelectionWith;
}

bool HasTranslationEngine(content::WebContents* web_contents) {
  return web_contents &&
         GetTranslationEngine(
             *ProfileOf(web_contents)->GetOriginalProfile()->GetPrefs()) !=
             TranslationEngine::kNone;
}

void AddTranslateMenuItems(content::WebContents* web_contents,
                           ui::SimpleMenuModel& menu,
                           ui::SimpleMenuModel::Delegate* delegate,
                           std::unique_ptr<ui::SimpleMenuModel>& submenu,
                           TranslateMenu kind) {
  if (!HasTranslationEngine(web_contents)) {
    menu.AddItemWithStringId(kSetUpTranslation, IDS_UW_TRANSLATE_SET_UP);
    return;
  }
  if (kind == TranslateMenu::kApp) {
    return;
  }
  const bool selection = kind == TranslateMenu::kSelection;
  submenu = std::make_unique<ui::SimpleMenuModel>(delegate);
  submenu->AddCheckItemWithStringId(
      selection ? kTranslateSelectionWithDevice : kTranslateWithDevice,
      IDS_UW_TRANSLATE_WITH_DEVICE);
  submenu->AddCheckItemWithStringId(selection
                                        ? kTranslateSelectionWithGoogleCloud
                                        : kTranslateWithGoogleCloud,
                                    IDS_UW_TRANSLATE_WITH_GOOGLE_CLOUD);
  submenu->AddCheckItemWithStringId(
      selection ? kTranslateSelectionWithAi : kTranslateWithAi,
      IDS_UW_TRANSLATE_WITH_AI);
  submenu->AddSeparator(ui::NORMAL_SEPARATOR);
  submenu->AddItemWithStringId(kSetUpTranslation, IDS_UW_TRANSLATE_SET_UP);
  menu.AddSubMenuWithStringId(
      selection ? kTranslateSelectionWith : kTranslateWith,
      IDS_UW_TRANSLATE_WITH, submenu.get());
}

bool IsTranslateMenuCommandEnabled(content::WebContents* web_contents,
                                   int command_id) {
  TranslationEngine engine = EngineOf(command_id);
  if (engine == TranslationEngine::kNone) {
    return true;
  }
  if (IsSelectionCommand(command_id)) {
    return IsTranslationEngineSetUp(ProfileOf(web_contents), engine);
  }
  translate::TranslateManager* manager = ManagerOf(web_contents);
  return manager && manager->CanManuallyTranslate() &&
         IsTranslationEngineSetUp(ProfileOf(web_contents), engine);
}

bool IsTranslateMenuCommandChecked(content::WebContents* web_contents,
                                   int command_id) {
  TranslationEngine engine = EngineOf(command_id);
  return engine != TranslationEngine::kNone &&
         engine ==
             GetTranslationEngine(
                 *ProfileOf(web_contents)->GetOriginalProfile()->GetPrefs());
}

void ExecuteTranslateMenuCommand(content::WebContents* web_contents,
                                 int command_id,
                                 const std::u16string& selection) {
  TranslationEngine engine = EngineOf(command_id);
  if (engine == TranslationEngine::kNone) {
    ShowTranslationSettings(web_contents);
    return;
  }
  translate::TranslateManager* manager = ManagerOf(web_contents);
  PageTranslator* translator = PageTranslator::FromWebContents(web_contents);
  if (!manager || !translator) {
    return;
  }
  if (IsSelectionCommand(command_id)) {
    tabs::TabInterface* tab =
        tabs::TabInterface::MaybeGetFromContents(web_contents);
    TranslateController* controller =
        tab ? TranslateController::From(tab->GetBrowserWindowInterface())
            : nullptr;
    if (!controller) {
      return;
    }
    std::string source;
    std::string target;
    ChromeTranslateClient::FromWebContents(web_contents)
        ->GetTranslateLanguages(web_contents, &source, &target,
                                /*for_display=*/false);
    translator->UseSelectionEngineOnce(engine);
    controller->StartPartialTranslate(source, target, selection);
    return;
  }
  if (manager->GetLanguageState()->IsPageTranslated()) {
    manager->RevertTranslation();
  }
  translator->UseEngineOnce(engine);
  manager->ShowTranslateUI(/*auto_translate=*/true,
                           /*triggered_from_menu=*/true);
}

void ShowTranslationSettings(content::WebContents* web_contents) {
  chrome::ScopedTabbedBrowserDisplayer displayer(
      ProfileOf(web_contents)->GetOriginalProfile());
  if (BrowserWindowInterface* browser = displayer.browser_window_interface()) {
    chrome::ShowSettingsSubPage(browser, chrome::kLanguageOptionsSubPage);
  }
}

}  // namespace uw
