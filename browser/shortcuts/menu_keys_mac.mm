// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/shortcuts/menu_keys.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/grit/branded_strings.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/command.h"
#include "ui/base/accelerators/command_constants.h"
#include "ui/base/accelerators/platform_accelerator_cocoa.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/workspaces/workspace_commands.h"

namespace uw {
namespace {

using Controller = TabOrganizationController;

// uw's commands of the menu bar, by the names the pref keeps their keys
// under. A name stays when its command's number changes.
constexpr struct {
  int command;
  const char* name;
} kNames[] = {
    {kNewTerminalCommand, "new_terminal"},
    {kNewChatCommand, "new_chat"},
    {kNewWorkspaceCommand, "new_workspace"},
    {kShowThemePicker, "theme"},
    {Controller::kNewChildTab, "new_child_tab"},
    {Controller::kNest, "nest_under_previous_tab"},
    {Controller::kOutdent, "move_up_one_level"},
    {Controller::kMoveUp, "move_branch_up"},
    {Controller::kMoveDown, "move_branch_down"},
    {Controller::kToggleBranch, "collapse_branch"},
    {Controller::kCloseBranch, "close_branch"},
    {IDC_CREATE_NEW_TAB_GROUP, "new_task"},
    {Controller::kNewTask, "move_to_new_task"},
    {Controller::kScratchpad, "move_to_scratchpad"},
    {Controller::kKeepHere, "keep_here"},
    {kGoToScratchpad, "go_to_scratchpad"},
    {kClearScratchpad, "clear_scratchpad"},
    {kRenameTask, "rename_task"},
    {kNameTask, "name_task"},
    {IDC_CLOSE_TAB_GROUP, "close_task"},
    {kConfirmSuggestion, "confirm_suggestion"},
    {Controller::kUndo, "undo_organization"},
    {kShowOrganizerActivity, "show_organizer_activity"},
    {kPauseOrganizationForAnHour, "pause_for_an_hour"},
    {kPauseOrganizationUntilLaunch, "pause_until_launch"},
    {kPauseOrganizationIndefinitely, "pause_indefinitely"},
    {kResumeOrganization, "resume_organization"},
    {kShowOrganizationSettings, "organization_settings"},
    {kShowShortcutSettings, "keyboard_shortcuts"},
};

// The name of one of uw's commands, or an empty one for Chromium's.
std::string NameOf(int command) {
  if (command >= kGoToTask1 && command <= kGoToTask9) {
    return base::StrCat(
        {"go_to_task_", base::NumberToString(command - kGoToTask1 + 1)});
  }
  for (const auto& entry : kNames) {
    if (entry.command == command) {
      return entry.name;
    }
  }
  return std::string();
}

// A key as the menu bar holds it: an item's key equivalent and modifier mask.
NSArray* KeyOf(NSMenuItem* item) {
  return @[ item.keyEquivalent, @(item.keyEquivalentModifierMask) ];
}

NSArray* NoKey() {
  return @[ @"", @0 ];
}

void SetKey(NSMenuItem* item, NSArray* key) {
  item.keyEquivalent = key[0];
  item.keyEquivalentModifierMask = [key[1] unsignedIntegerValue];
}

// The key an item had before uw changed it: always kept for uw's commands,
// and kept for one of Chromium's while a uw command holds its key.
char kOwnKey;

NSArray* OwnKey(NSMenuItem* item) {
  return objc_getAssociatedObject(item, &kOwnKey);
}

void KeepOwnKey(NSMenuItem* item, NSArray* key) {
  objc_setAssociatedObject(item, &kOwnKey, key,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

// The name of a key the recorder does not take, or of a character.
std::string KeyName(unichar character) {
  if (character >= NSF1FunctionKey && character <= NSF35FunctionKey) {
    return base::StrCat(
        {"F", base::NumberToString(character - NSF1FunctionKey + 1)});
  }
  switch (character) {
    case NSUpArrowFunctionKey:
      return ui::kKeyUp;
    case NSDownArrowFunctionKey:
      return ui::kKeyDown;
    case NSLeftArrowFunctionKey:
      return ui::kKeyLeft;
    case NSRightArrowFunctionKey:
      return ui::kKeyRight;
    case NSHomeFunctionKey:
      return ui::kKeyHome;
    case NSEndFunctionKey:
      return ui::kKeyEnd;
    case NSPageUpFunctionKey:
      return ui::kKeyPgUp;
    case NSPageDownFunctionKey:
      return ui::kKeyPgDwn;
    case NSInsertFunctionKey:
      return ui::kKeyIns;
    case NSDeleteFunctionKey:
      return ui::kKeyDel;
    case ' ':
      return ui::kKeySpace;
    case '\t':
      return ui::kKeyTab;
    case ',':
      return ui::kKeyComma;
    case '.':
      return ui::kKeyPeriod;
    case '\r':
      return "Return";
    case 0x1b:
      return "Esc";
    case 0x08:
    case 0x7f:
      return "Backspace";
    case '+':
      return "Plus";
  }
  return base::SysNSStringToUTF8([NSString stringWithCharacters:&character
                                                         length:1]);
}

// `key` written as Chromium's key recorder writes one, such as
// "Command+Shift+T", or empty for no key.
std::string TextOf(NSArray* key) {
  NSString* equivalent = key[0];
  if (!equivalent.length) {
    return std::string();
  }
  NSEventModifierFlags mask = [key[1] unsignedIntegerValue];
  const unichar character = [equivalent characterAtIndex:0];
  std::string name;
  if (character >= 'A' && character <= 'Z') {
    mask |= NSEventModifierFlagShift;
    name = std::string(1, static_cast<char>(character));
  } else if (character >= 'a' && character <= 'z') {
    name = std::string(1, static_cast<char>(character - 'a' + 'A'));
  } else {
    name = KeyName(character);
  }
  std::vector<std::string> parts;
  if (mask & NSEventModifierFlagCommand) {
    parts.push_back(ui::kKeyCommand);
  }
  if (mask & NSEventModifierFlagControl) {
    parts.push_back(ui::kKeyCtrl);
  }
  if (mask & NSEventModifierFlagOption) {
    parts.push_back(ui::kKeyAlt);
  }
  if (mask & NSEventModifierFlagShift) {
    parts.push_back(ui::kKeyShift);
  }
  parts.push_back(name);
  return base::JoinString(parts, ui::kKeySeparator);
}

// The key the recorder wrote as `text`, no key for an empty one, or nil for
// one that is not a key with Command.
NSArray* KeyFromText(std::string_view text) {
  if (text.empty()) {
    return NoKey();
  }
  const ui::Accelerator accelerator = ui::Command::StringToAccelerator(text);
  if (accelerator.key_code() == ui::VKEY_UNKNOWN || !accelerator.IsCmdDown()) {
    return nil;
  }
  KeyEquivalentAndModifierMask* key =
      ui::GetKeyEquivalentAndModifierMaskFromAccelerator(accelerator);
  return @[ key.keyEquivalent, @(key.modifierMask) ];
}

// Calls `visit` with every command of `menu` and its submenus, and the menus
// it is in. A command runs from the menu or has a key; the pages the History,
// Bookmarks, and Window menus list do neither. The Services menu belongs to
// other apps.
void ForEachCommand(
    NSMenu* menu,
    const std::string& path,
    base::FunctionRef<void(NSMenuItem*, const std::string&)> visit) {
  static SEL const kDispatch = NSSelectorFromString(@"commandDispatch:");
  for (NSMenuItem* item in menu.itemArray) {
    if (item.separatorItem || item.hidden ||
        (item.submenu && item.submenu == NSApp.servicesMenu)) {
      continue;
    }
    if (item.submenu) {
      // The menu bar names the app menu after the app, whatever its item says.
      const std::string title = item.tag == kMacChromeMenuId
                                    ? l10n_util::GetStringUTF8(IDS_PRODUCT_NAME)
                                    : base::SysNSStringToUTF8(item.title);
      ForEachCommand(item.submenu,
                     path.empty() ? title : base::StrCat({path, " › ", title}),
                     visit);
    } else if (item.action == kDispatch || item.keyEquivalent.length ||
               OwnKey(item)) {
      visit(item, path);
    }
  }
}

void ForEachCommand(
    base::FunctionRef<void(NSMenuItem*, const std::string&)> visit) {
  ForEachCommand(NSApp.mainMenu, std::string(), visit);
}

NSMenuItem* ItemNamed(std::string_view name) {
  NSMenuItem* found = nil;
  ForEachCommand([&](NSMenuItem* item, const std::string&) {
    if (!found && NameOf(item.tag) == name) {
      found = item;
    }
  });
  return found;
}

std::string DefaultOf(NSMenuItem* item) {
  NSArray* own = OwnKey(item);
  return TextOf(own ? own : KeyOf(item));
}

// Takes `key` from every one of uw's commands that holds it, except `keep`.
// Chromium's commands lose it when the menu bar is given the keys.
void TakeKey(base::DictValue& keys,
             std::string_view key,
             std::string_view keep) {
  ForEachCommand([&](NSMenuItem* item, const std::string&) {
    const std::string name = NameOf(item.tag);
    if (!name.empty() && name != keep && TextOf(KeyOf(item)) == key) {
      keys.Set(name, "");
    }
  });
}

// The commands whose keys differ from their defaults, with the keys they have.
std::map<int, std::string>& ChangedKeys() {
  static base::NoDestructor<std::map<int, std::string>> changed;
  return *changed;
}

}  // namespace

void ApplyMenuKeys(const PrefService& local_state) {
  const base::DictValue& keys = local_state.GetDict(kMenuKeysPref);
  std::vector<NSMenuItem*> items;
  std::set<std::string> held;
  ForEachCommand([&](NSMenuItem* item, const std::string&) {
    items.push_back(item);
    const std::string name = NameOf(item.tag);
    if (name.empty()) {
      return;
    }
    if (!OwnKey(item)) {
      KeepOwnKey(item, KeyOf(item));
    }
    const std::string* text = keys.FindString(name);
    NSArray* key = text ? KeyFromText(*text) : nil;
    if (key && [key[0] length]) {
      held.insert(TextOf(key));
    }
  });

  ChangedKeys().clear();
  for (NSMenuItem* item : items) {
    const std::string name = NameOf(item.tag);
    const std::string* text = name.empty() ? nullptr : keys.FindString(name);
    NSArray* own = OwnKey(item) ? OwnKey(item) : KeyOf(item);
    NSArray* key = text ? KeyFromText(*text) : nil;
    if (!key) {
      key = held.contains(TextOf(own)) ? NoKey() : own;
    }
    SetKey(item, key);
    if (name.empty()) {
      KeepOwnKey(item, key == own ? nil : own);
    }
    if (TextOf(key) != TextOf(own)) {
      ChangedKeys()[item.tag] = TextOf(key);
    }
  }
}

std::vector<MenuKey> MenuKeys() {
  std::vector<MenuKey> keys;
  ForEachCommand([&](NSMenuItem* item, const std::string& menu) {
    keys.push_back({.menu = menu,
                    .title = base::SysNSStringToUTF8(item.title),
                    .name = NameOf(item.tag),
                    .key = TextOf(KeyOf(item)),
                    .default_key = DefaultOf(item)});
  });
  for (MenuKey& key : keys) {
    if (!key.key.empty() || key.default_key.empty()) {
      continue;
    }
    for (const MenuKey& other : keys) {
      if (other.key == key.default_key) {
        key.taken_by = other.title;
        break;
      }
    }
  }
  return keys;
}

bool SetMenuKey(PrefService& local_state,
                std::string_view name,
                std::string_view key) {
  NSMenuItem* item = ItemNamed(name);
  NSArray* wanted = KeyFromText(key);
  if (!item || !wanted) {
    return false;
  }
  const std::string text = TextOf(wanted);
  {
    ScopedDictPrefUpdate update(&local_state, kMenuKeysPref);
    if (!text.empty()) {
      TakeKey(*update, text, name);
    }
    if (text == DefaultOf(item)) {
      update->Remove(name);
    } else {
      update->Set(name, std::string(key));
    }
  }
  ApplyMenuKeys(local_state);
  return true;
}

void ResetMenuKey(PrefService& local_state, std::string_view name) {
  NSMenuItem* item = ItemNamed(name);
  if (!item) {
    return;
  }
  {
    ScopedDictPrefUpdate update(&local_state, kMenuKeysPref);
    update->Remove(name);
    if (const std::string key = DefaultOf(item); !key.empty()) {
      TakeKey(*update, key, name);
    }
  }
  ApplyMenuKeys(local_state);
}

void GiveBackMenuKey(PrefService& local_state, std::string_view key) {
  if (key.empty()) {
    return;
  }
  {
    ScopedDictPrefUpdate update(&local_state, kMenuKeysPref);
    TakeKey(*update, key, std::string_view());
  }
  ApplyMenuKeys(local_state);
}

void ResetMenuKeys(PrefService& local_state) {
  local_state.ClearPref(kMenuKeysPref);
  ApplyMenuKeys(local_state);
}

bool MenuKeyChanged(int command, ui::Accelerator* key) {
  const auto changed = ChangedKeys().find(command);
  if (changed == ChangedKeys().end()) {
    return false;
  }
  if (key) {
    *key = changed->second.empty()
               ? ui::Accelerator()
               : ui::Command::StringToAccelerator(changed->second);
  }
  return true;
}

}  // namespace uw
