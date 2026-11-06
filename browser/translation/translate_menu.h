// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_
#define UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_

#include <memory>

#include "ui/menus/simple_menu_model.h"

namespace content {
class WebContents;
}

namespace uw {

// The translation items of a page's right-click menu and the app menu, after
// "Translate to <language>": "Set up translation…" while the profile has no
// translation engine, else "Translate with" and the engines. See "Ways in" in
// docs/plans/page-translation.md. The numbers follow
// TabOrganizationController::Command and WindowCommand.
enum TranslateMenuCommand {
  kSetUpTranslation = 56300,
  kTranslateWithDevice,
  kTranslateWithGoogleCloud,
  kTranslateWithAi,
  kTranslateWith,
};

bool IsTranslateMenuCommand(int command_id);

// Whether "Translate to <language>" can run: only with an engine.
bool HasTranslationEngine(content::WebContents* web_contents);

// Adds the items after "Translate to <language>". `submenu` keeps the
// "Translate with" submenu, whose commands go to `delegate`.
void AddTranslateMenuItems(content::WebContents* web_contents,
                           ui::SimpleMenuModel& menu,
                           ui::SimpleMenuModel::Delegate* delegate,
                           std::unique_ptr<ui::SimpleMenuModel>& submenu,
                           bool with_submenu);

bool IsTranslateMenuCommandEnabled(content::WebContents* web_contents,
                                   int command_id);
bool IsTranslateMenuCommandChecked(content::WebContents* web_contents,
                                   int command_id);
void ExecuteTranslateMenuCommand(content::WebContents* web_contents,
                                 int command_id);

// Opens Settings › Languages, where the engine is picked.
void ShowTranslationSettings(content::WebContents* web_contents);

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_TRANSLATE_MENU_H_
