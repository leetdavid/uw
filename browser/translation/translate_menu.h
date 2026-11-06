// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_
#define UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_

#include <memory>
#include <string>

#include "ui/menus/simple_menu_model.h"

namespace content {
class WebContents;
}

namespace uw {

// The translation items of a page's right-click menu and the app menu, after
// "Translate to <language>" or "Translate “<selection>”": "Set up
// translation…" while the profile has no translation engine, else "Translate
// with" and the engines. See "Ways in" in docs/plans/page-translation.md. The
// numbers follow TabOrganizationController::Command and WindowCommand.
enum TranslateMenuCommand {
  kSetUpTranslation = 56300,
  kTranslateWithDevice,
  kTranslateWithGoogleCloud,
  kTranslateWithAi,
  kTranslateWith,
  kTranslateSelectionWithDevice,
  kTranslateSelectionWithGoogleCloud,
  kTranslateSelectionWithAi,
  kTranslateSelectionWith,
};

// Which menu the items go in: the app menu has no "Translate with".
enum class TranslateMenu {
  kApp,
  kPage,
  kSelection,
};

bool IsTranslateMenuCommand(int command_id);

// Whether "Translate to <language>" and "Translate “<selection>”" can run:
// only with an engine.
bool HasTranslationEngine(content::WebContents* web_contents);

// Adds the items after the translate item of `kind`. `submenu` keeps the
// "Translate with" submenu, whose commands go to `delegate`.
void AddTranslateMenuItems(content::WebContents* web_contents,
                           ui::SimpleMenuModel& menu,
                           ui::SimpleMenuModel::Delegate* delegate,
                           std::unique_ptr<ui::SimpleMenuModel>& submenu,
                           TranslateMenu kind);

bool IsTranslateMenuCommandEnabled(content::WebContents* web_contents,
                                   int command_id);
bool IsTranslateMenuCommandChecked(content::WebContents* web_contents,
                                   int command_id);
// `selection` is the selected text the menu opened on, for the
// kTranslateSelectionWith commands.
void ExecuteTranslateMenuCommand(content::WebContents* web_contents,
                                 int command_id,
                                 const std::u16string& selection);

// Opens Settings › Languages, where the engine is picked.
void ShowTranslationSettings(content::WebContents* web_contents);

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_
