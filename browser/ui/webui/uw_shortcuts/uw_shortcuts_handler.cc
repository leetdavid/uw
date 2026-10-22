// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_shortcuts/uw_shortcuts_handler.h"

#include <utility>
#include <vector>

#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/webui/web_ui_util.h"
#include "uw/browser/shortcuts/menu_keys.h"

namespace uw {
namespace {

std::vector<mojom::MenuKeyPtr> AllMenuKeys() {
  std::vector<mojom::MenuKeyPtr> keys;
  for (const MenuKey& key : MenuKeys()) {
    keys.push_back(mojom::MenuKey::New(key.menu, key.title, key.name, key.key,
                                       key.default_key, key.taken_by));
  }
  return keys;
}

PrefService& LocalState() {
  return *g_browser_process->local_state();
}

}  // namespace

UwShortcutsHandler::UwShortcutsHandler(
    mojo::PendingReceiver<mojom::ShortcutsHandler> receiver)
    : receiver_(this, std::move(receiver)) {}

UwShortcutsHandler::~UwShortcutsHandler() = default;

// static
void UwShortcutsHandler::AddLoadTimeData(
    content::WebUIDataSource* data_source) {
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"uwShortcutsPageTitle", IDS_UW_SETTINGS_SHORTCUTS_TITLE},
      {"uwShortcutsIntro", IDS_UW_SETTINGS_SHORTCUTS_INTRO},
      {"uwShortcutsResetAll", IDS_UW_SETTINGS_SHORTCUTS_RESET_ALL},
      {"uwShortcutsReset", IDS_UW_SETTINGS_SHORTCUTS_RESET},
      {"uwShortcutsResetLabel", IDS_UW_SETTINGS_SHORTCUTS_RESET_LABEL},
      {"uwShortcutsNoKey", IDS_UW_SETTINGS_SHORTCUTS_NO_KEY},
      {"uwShortcutsChromiumKey", IDS_UW_SETTINGS_SHORTCUTS_CHROMIUM_KEY},
      {"uwShortcutsWentTo", IDS_UW_SETTINGS_SHORTCUTS_WENT_TO},
      {"uwShortcutsInputLabel", IDS_UW_SETTINGS_SHORTCUTS_INPUT_LABEL},
      {"uwShortcutsEditLabel", IDS_UW_SETTINGS_SHORTCUTS_EDIT_LABEL},
      {"uwShortcutsClearLabel", IDS_UW_SETTINGS_SHORTCUTS_CLEAR_LABEL},
      // The key recorder's own words for a missing key and a key without
      // Command, which every key recorder of settings shares.
      {"shortcutNotSet", IDS_UW_SETTINGS_SHORTCUTS_NO_KEY},
      {"shortcutIncludeStartModifier",
       IDS_UW_SETTINGS_SHORTCUTS_INCLUDE_COMMAND},
  };
  data_source->AddLocalizedStrings(kLocalizedStrings);
}

void UwShortcutsHandler::GetMenuKeys(GetMenuKeysCallback callback) {
  std::move(callback).Run(AllMenuKeys());
}

void UwShortcutsHandler::SetMenuKey(const std::string& name,
                                    const std::string& key,
                                    SetMenuKeyCallback callback) {
  uw::SetMenuKey(LocalState(), name, key);
  std::move(callback).Run(AllMenuKeys());
}

void UwShortcutsHandler::ResetMenuKey(const std::string& name,
                                      ResetMenuKeyCallback callback) {
  uw::ResetMenuKey(LocalState(), name);
  std::move(callback).Run(AllMenuKeys());
}

void UwShortcutsHandler::GiveBackMenuKey(const std::string& key,
                                         GiveBackMenuKeyCallback callback) {
  uw::GiveBackMenuKey(LocalState(), key);
  std::move(callback).Run(AllMenuKeys());
}

void UwShortcutsHandler::ResetMenuKeys(ResetMenuKeysCallback callback) {
  uw::ResetMenuKeys(LocalState());
  std::move(callback).Run(AllMenuKeys());
}

}  // namespace uw
