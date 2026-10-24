// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_UW_COMMAND_COMMAND_SETTINGS_H_
#define UW_COMPONENTS_UW_COMMAND_COMMAND_SETTINGS_H_

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace uw {

struct Theme;

// The settings `uw settings` reads and changes: uw's own, each with a control
// on one of uw's pages, by readable names. See docs/plans/uw-command.md.
struct CommandSetting {
  // Who shares a setting: the workspaces of one profile, one workspace, or
  // every workspace.
  enum class Scope { kProfile, kWorkspace, kEverywhere };
  enum class Kind { kSwitch, kNumber, kWhole, kChoice, kCurrency };

  // A choice as typed, and as uw stores it.
  struct Choice {
    std::string_view name;
    std::string_view stored;
  };

  std::string_view name;
  Scope scope;
  Kind kind;
  // The preference behind a profile's or every workspace's setting.
  std::string_view pref;
  double min = 0;
  double max = 0;
  // RAW_PTR_EXCLUSION: #constexpr-ctor-field-initializer, the table is static.
  RAW_PTR_EXCLUSION base::span<const Choice> choices;
};

base::span<const CommandSetting> CommandSettings();
const CommandSetting* FindCommandSetting(std::string_view name);

// What a setting takes, such as "0 to 100" or "ask-first, auto, full-auto".
std::string Takes(const CommandSetting& setting);

// The value to store for `text`, or the sentence that says what the setting
// takes. A currency is checked for its form only; uw checks the code.
base::expected<base::Value, std::string> ParseSettingValue(
    const CommandSetting& setting,
    std::string_view text);

// A stored value as the command shows it: choices by their typed names, and
// an empty currency as "auto".
base::Value ShownSettingValue(const CommandSetting& setting,
                              const base::Value& stored);

// A workspace's setting lives in its theme.
base::Value ThemeSettingValue(const Theme& theme,
                              const CommandSetting& setting);
void SetThemeSetting(Theme& theme,
                     const CommandSetting& setting,
                     const base::Value& stored);

}  // namespace uw

#endif  // UW_COMPONENTS_UW_COMMAND_COMMAND_SETTINGS_H_
