// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/uw_command/command_settings.h"

#include <set>
#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

const CommandSetting& Setting(std::string_view name) {
  const CommandSetting* setting = FindCommandSetting(name);
  CHECK(setting) << name;
  return *setting;
}

std::string Refusal(std::string_view name, std::string_view text) {
  base::expected<base::Value, std::string> parsed =
      ParseSettingValue(Setting(name), text);
  return parsed.has_value() ? std::string() : parsed.error();
}

TEST(CommandSettingsTest, NamesAreUniqueAndEachHasAHome) {
  std::set<std::string_view> names;
  for (const CommandSetting& setting : CommandSettings()) {
    EXPECT_TRUE(names.insert(setting.name).second) << setting.name;
    EXPECT_EQ(setting.pref.empty(),
              setting.scope == CommandSetting::Scope::kWorkspace)
        << setting.name;
  }
  EXPECT_EQ(FindCommandSetting("organizer.nope"), nullptr);
}

TEST(CommandSettingsTest, SwitchesTakeTrueOrFalse) {
  EXPECT_EQ(ParseSettingValue(Setting("organizer.enabled"), "false").value(),
            base::Value(false));
  EXPECT_EQ(Refusal("organizer.enabled", "off"),
            "organizer.enabled takes true, false.");
}

TEST(CommandSettingsTest, ThresholdsTakeStepsOfFiveHundredths) {
  const CommandSetting& move = Setting("organizer.move-threshold");
  EXPECT_EQ(ParseSettingValue(move, "0.7").value(), base::Value(0.7));
  EXPECT_EQ(ParseSettingValue(move, "0.95").value(), base::Value(0.95));
  EXPECT_EQ(ParseSettingValue(move, "0.50").value(), base::Value(0.5));
  EXPECT_EQ(Refusal("organizer.move-threshold", "0.72"),
            "organizer.move-threshold takes 0.50 to 0.95, in steps of 0.05.");
  EXPECT_FALSE(ParseSettingValue(move, "1").has_value());
  EXPECT_FALSE(ParseSettingValue(move, "0.45").has_value());
  EXPECT_FALSE(ParseSettingValue(move, "high").has_value());
}

TEST(CommandSettingsTest, PercentsTakeWholeNumbersUpToAHundred) {
  EXPECT_EQ(ParseSettingValue(Setting("theme.grain"), "20").value(),
            base::Value(20));
  EXPECT_EQ(Refusal("theme.grain", "140"), "theme.grain takes 0 to 100.");
  EXPECT_FALSE(ParseSettingValue(Setting("theme.grain"), "2.5").has_value());
}

TEST(CommandSettingsTest, ChoicesAreTypedWithHyphensAndStoredAsUwStoresThem) {
  const CommandSetting& mode = Setting("agent.approval-mode");
  EXPECT_EQ(ParseSettingValue(mode, "full-auto").value(),
            base::Value("full_auto"));
  EXPECT_EQ(ShownSettingValue(mode, base::Value("ask_first")),
            base::Value("ask-first"));
  EXPECT_EQ(Refusal("agent.approval-mode", "full_auto"),
            "agent.approval-mode takes ask-first, auto, full-auto.");
}

TEST(CommandSettingsTest, CurrencyIsACodeOrAuto) {
  const CommandSetting& currency = Setting("command-bar.currency");
  EXPECT_EQ(ParseSettingValue(currency, "hkd").value(), base::Value("HKD"));
  EXPECT_EQ(ParseSettingValue(currency, "auto").value(), base::Value(""));
  EXPECT_EQ(ShownSettingValue(currency, base::Value("")), base::Value("auto"));
  EXPECT_FALSE(ParseSettingValue(currency, "dollars").has_value());
}

TEST(CommandSettingsTest, AWorkspaceSettingReadsAndChangesItsTheme) {
  Theme theme;
  SetThemeSetting(theme, Setting("theme.grain"), base::Value(40));
  SetThemeSetting(theme, Setting("theme.page-mode"), base::Value("dark"));
  SetThemeSetting(theme, Setting("theme.colors-in-dark-mode"),
                  base::Value(true));
  EXPECT_EQ(theme.grain, 40);
  EXPECT_EQ(theme.page_mode, PageMode::kDark);
  EXPECT_TRUE(theme.colors_in_dark_mode);
  EXPECT_EQ(ThemeSettingValue(theme, Setting("theme.grain")), base::Value(40));
  EXPECT_EQ(ThemeSettingValue(theme, Setting("theme.mode")),
            base::Value("auto"));
  SetThemeSetting(theme, Setting("theme.page-mode"), base::Value("theme"));
  EXPECT_EQ(theme.page_mode, PageMode::kTheme);
  EXPECT_EQ(ThemeSettingValue(theme, Setting("theme.page-mode")),
            base::Value("theme"));
  EXPECT_EQ(ThemeSettingValue(theme, Setting("theme.intensity")),
            base::Value(kFullIntensity));
}

}  // namespace
}  // namespace uw
