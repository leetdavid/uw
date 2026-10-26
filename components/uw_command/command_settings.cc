// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/uw_command/command_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "uw/components/look/theme.h"

namespace uw {

namespace {

using Scope = CommandSetting::Scope;
using Kind = CommandSetting::Kind;
using Choice = CommandSetting::Choice;

// Thresholds move in steps of 0.05.
constexpr double kStepsPerUnit = 20;

constexpr Choice kApprovalModes[] = {
    {"ask-first", "ask_first"}, {"auto", "auto"}, {"full-auto", "full_auto"}};
constexpr Choice kThemeModes[] = {
    {"auto", "auto"}, {"light", "light"}, {"dark", "dark"}};
constexpr Choice kPageModes[] = {
    {"theme", "theme"}, {"light", "light"}, {"dark", "dark"}};

constexpr CommandSetting Switch(std::string_view name,
                                Scope scope,
                                std::string_view pref = {}) {
  return {.name = name, .scope = scope, .kind = Kind::kSwitch, .pref = pref};
}

constexpr CommandSetting Threshold(std::string_view name,
                                   std::string_view pref) {
  return {.name = name,
          .scope = Scope::kProfile,
          .kind = Kind::kNumber,
          .pref = pref,
          .min = 0.5,
          .max = 0.95};
}

constexpr CommandSetting Percent(std::string_view name) {
  return {.name = name,
          .scope = Scope::kWorkspace,
          .kind = Kind::kWhole,
          .min = 0,
          .max = 100};
}

constexpr CommandSetting Choices(std::string_view name,
                                 Scope scope,
                                 base::span<const Choice> choices,
                                 std::string_view pref = {}) {
  return {.name = name,
          .scope = scope,
          .kind = Kind::kChoice,
          .pref = pref,
          .choices = choices};
}

constexpr auto kSettings = std::to_array<CommandSetting>({
    Switch("organizer.enabled", Scope::kProfile, "uw.organization.enabled"),
    Switch("organizer.show-activity", Scope::kProfile,
           "uw.organization.show_activity"),
    Threshold("organizer.move-threshold", "uw.organization.threshold.move"),
    Threshold("organizer.new-task-threshold",
              "uw.organization.threshold.new_task"),
    Threshold("organizer.rename-threshold", "uw.organization.threshold.rename"),
    Choices("agent.approval-mode", Scope::kProfile, kApprovalModes,
            "uw.agent.approval_mode"),
    Switch("agent.memory", Scope::kProfile, "uw.agent.memory"),
    Switch("command-bar.enabled", Scope::kProfile, "uw.command_bar.enabled"),
    {.name = "command-bar.currency",
     .scope = Scope::kProfile,
     .kind = Kind::kCurrency,
     .pref = "uw.command_bar.currency"},
    Switch("mouse.option-click-closes-branch", Scope::kProfile,
           "uw.mouse.option_click_closes_branch"),
    Switch("mouse.task-row-new-tab", Scope::kProfile,
           "uw.mouse.task_row_new_tab"),
    Switch("mouse.double-click-opens-tab", Scope::kProfile,
           "uw.mouse.double_click_opens_tab"),
    Switch("mouse.middle-click-closes-task", Scope::kProfile,
           "uw.mouse.middle_click_closes_task"),
    Switch("glass.windows-behind", Scope::kEverywhere,
           "uw.glass.windows_behind"),
    Choices("theme.mode", Scope::kWorkspace, kThemeModes),
    Choices("theme.page-mode", Scope::kWorkspace, kPageModes),
    Percent("theme.intensity"),
    Percent("theme.grain"),
    Percent("theme.transparency"),
    Percent("theme.glass"),
    Switch("theme.colors-in-dark-mode", Scope::kWorkspace),
});

std::string ChoiceNames(base::span<const Choice> choices) {
  std::vector<std::string_view> names;
  for (const Choice& choice : choices) {
    names.push_back(choice.name);
  }
  return base::JoinString(names, ", ");
}

base::unexpected<std::string> Refuse(const CommandSetting& setting) {
  return base::unexpected(
      base::StrCat({setting.name, " takes ", Takes(setting), "."}));
}

std::string_view ModeName(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kAuto:
      return "auto";
    case ThemeMode::kLight:
      return "light";
    case ThemeMode::kDark:
      return "dark";
  }
}

std::string_view PageModeName(PageMode mode) {
  switch (mode) {
    case PageMode::kTheme:
      return "theme";
    case PageMode::kLight:
      return "light";
    case PageMode::kDark:
      return "dark";
  }
}

int* PercentOf(Theme& theme, std::string_view name) {
  if (name == "theme.intensity") {
    return &theme.intensity;
  }
  if (name == "theme.grain") {
    return &theme.grain;
  }
  if (name == "theme.transparency") {
    return &theme.transparency;
  }
  if (name == "theme.glass") {
    return &theme.glass;
  }
  return nullptr;
}

}  // namespace

base::span<const CommandSetting> CommandSettings() {
  return kSettings;
}

const CommandSetting* FindCommandSetting(std::string_view name) {
  auto found = std::ranges::find(kSettings, name, &CommandSetting::name);
  return found == kSettings.end() ? nullptr : &*found;
}

std::string Takes(const CommandSetting& setting) {
  switch (setting.kind) {
    case Kind::kSwitch:
      return "true, false";
    case Kind::kNumber:
      return base::StringPrintf("%.2f to %.2f, in steps of %.2f", setting.min,
                                setting.max, 1 / kStepsPerUnit);
    case Kind::kWhole:
      return base::StringPrintf("%.0f to %.0f", setting.min, setting.max);
    case Kind::kChoice:
      return ChoiceNames(setting.choices);
    case Kind::kCurrency:
      return "a currency code, such as EUR, or auto";
  }
}

base::expected<base::Value, std::string> ParseSettingValue(
    const CommandSetting& setting,
    std::string_view text) {
  switch (setting.kind) {
    case Kind::kSwitch:
      if (text == "true" || text == "false") {
        return base::Value(text == "true");
      }
      return Refuse(setting);
    case Kind::kNumber: {
      double number;
      if (!base::StringToDouble(text, &number)) {
        return Refuse(setting);
      }
      const double steps = number * kStepsPerUnit;
      if (std::abs(steps - std::round(steps)) > 1e-6 ||
          std::round(steps) < setting.min * kStepsPerUnit ||
          std::round(steps) > setting.max * kStepsPerUnit) {
        return Refuse(setting);
      }
      return base::Value(std::round(steps) / kStepsPerUnit);
    }
    case Kind::kWhole: {
      int number;
      if (!base::StringToInt(text, &number) || number < setting.min ||
          number > setting.max) {
        return Refuse(setting);
      }
      return base::Value(number);
    }
    case Kind::kChoice:
      for (const Choice& choice : setting.choices) {
        if (choice.name == text) {
          return base::Value(choice.stored);
        }
      }
      return Refuse(setting);
    case Kind::kCurrency:
      if (text == "auto") {
        return base::Value("");
      }
      if (text.size() == 3 && std::ranges::all_of(text, base::IsAsciiAlpha<char>)) {
        return base::Value(base::ToUpperASCII(text));
      }
      return Refuse(setting);
  }
}

base::Value ShownSettingValue(const CommandSetting& setting,
                              const base::Value& stored) {
  if (setting.kind == Kind::kChoice && stored.is_string()) {
    for (const Choice& choice : setting.choices) {
      if (choice.stored == stored.GetString()) {
        return base::Value(choice.name);
      }
    }
  }
  if (setting.kind == Kind::kCurrency && stored.is_string() &&
      stored.GetString().empty()) {
    return base::Value("auto");
  }
  return stored.Clone();
}

base::Value ThemeSettingValue(const Theme& theme,
                              const CommandSetting& setting) {
  if (setting.name == "theme.mode") {
    return base::Value(ModeName(theme.mode));
  }
  if (setting.name == "theme.page-mode") {
    return base::Value(PageModeName(theme.page_mode));
  }
  if (setting.name == "theme.colors-in-dark-mode") {
    return base::Value(theme.colors_in_dark_mode);
  }
  Theme copy = theme;
  int* percent = PercentOf(copy, setting.name);
  return percent ? base::Value(*percent) : base::Value();
}

void SetThemeSetting(Theme& theme,
                     const CommandSetting& setting,
                     const base::Value& stored) {
  if (setting.name == "theme.mode" && stored.is_string()) {
    const std::string& mode = stored.GetString();
    theme.mode = mode == "light"  ? ThemeMode::kLight
                 : mode == "dark" ? ThemeMode::kDark
                                  : ThemeMode::kAuto;
  } else if (setting.name == "theme.page-mode" && stored.is_string()) {
    const std::string& mode = stored.GetString();
    theme.page_mode = mode == "light"  ? PageMode::kLight
                      : mode == "dark" ? PageMode::kDark
                                       : PageMode::kTheme;
  } else if (setting.name == "theme.colors-in-dark-mode" && stored.is_bool()) {
    theme.colors_in_dark_mode = stored.GetBool();
  } else if (int* percent = PercentOf(theme, setting.name);
             percent && stored.is_int()) {
    *percent = stored.GetInt();
  }
}

}  // namespace uw
