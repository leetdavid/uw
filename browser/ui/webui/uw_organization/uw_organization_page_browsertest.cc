// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/look/tint.h"
#include "uw/components/tab_organization/placement.h"

namespace uw {
namespace {

constexpr char kHelpers[] = R"(
  const find = (root, selector) => {
    const found = root.querySelector(selector);
    if (found) {
      return found;
    }
    for (const element of root.querySelectorAll('*')) {
      const inner = element.shadowRoot && find(element.shadowRoot, selector);
      if (inner) {
        return inner;
      }
    }
    return null;
  };
  const until = (probe) => new Promise((resolve) => {
    const check = () => {
      const value = probe();
      if (value) {
        resolve(value);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
  const page = () => find(document, 'settings-uw-organization-page');
  const part = (id) => page()?.shadowRoot?.getElementById(id) ?? null;
  const text = (id) => part(id).textContent.trim();
)";

// Drives the real uw page of chrome://settings.
class UwOrganizationPageBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  static std::string String(int id) { return l10n_util::GetStringUTF8(id); }

  // Runs `body` as the body of an async function inside the settings page.
  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  void OpenPage() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/uwOrganization")));
    // The thresholds show once the page has loaded the saved state.
    ASSERT_EQ(true, Run("await until(() => part('moveThreshold'));"
                        "return true;"));
  }

  std::string TextOf(const std::string& id) {
    return Run(content::JsReplace("return text($1);", id)).ExtractString();
  }

  // Clicks the automatic organization switch and waits for the status line
  // to change.
  void ClickSwitch() {
    ASSERT_EQ(true, Run("const before = text('status');"
                        "part('enabledToggle').click();"
                        "await until(() => text('status') !== before);"
                        "return true;"));
  }

  ThemeService* theme() {
    return ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  }

  SkColor FrameColor() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetColorProvider()
        ->GetColor(ui::kColorFrameActive);
  }

  static std::string Hex(SkColor color) {
    return base::StringPrintf("#%02X%02X%02X", SkColorGetR(color),
                              SkColorGetG(color), SkColorGetB(color));
  }
};

// The models are picked and tested on uw://providers. The switch turns
// automatic organization on, and the status says what it still waits for.
IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       TheSwitchWaitsForAPickedAndTestedModel) {
  OpenPage();
  EXPECT_EQ(TextOf("status"), String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_OFF));
  EXPECT_EQ(TextOf("statusDetail"),
            String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_HINT));
  EXPECT_EQ(true, Run("return part('pauseSection').hidden;"));

  ClickSwitch();
  EXPECT_TRUE(prefs()->GetBoolean(kOrganizationEnabledPref));
  EXPECT_EQ(true, Run("await until(() => !part('pauseSection').hidden);"
                      "return true;"));
  EXPECT_EQ(TextOf("status"),
            String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_WAITING));
  EXPECT_EQ(TextOf("statusDetail"),
            String(IDS_UW_SETTINGS_ORGANIZATION_WAITING_NO_MODEL));

  const std::string ollama =
      AddProvider(*prefs(), PresetProvider(ProviderPreset::kOllama));
  SetChoice(*prefs(), ModelSlot::kLanguage, ollama, "llama3.2");
  OpenPage();
  EXPECT_EQ(TextOf("statusDetail"),
            String(IDS_UW_SETTINGS_ORGANIZATION_WAITING_LANGUAGE_TEST));
  EXPECT_FALSE(OrganizationReady(prefs()));

  MarkTested(*prefs(), ModelSlot::kLanguage,
             *ReadChoice(*prefs(), ModelSlot::kLanguage));
  OpenPage();
  EXPECT_EQ(TextOf("status"), String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_ON));
  EXPECT_EQ(
      TextOf("statusDetail"),
      l10n_util::GetStringFUTF8(
          IDS_UW_SETTINGS_ORGANIZATION_STATUS_DECIDES_AND_NAMES, u"llama3.2"));
  EXPECT_TRUE(OrganizationReady(prefs()));
  SaveWindowScreenshot(browser(), "settings-organization");

  ClickSwitch();
  EXPECT_FALSE(prefs()->GetBoolean(kOrganizationEnabledPref));
  EXPECT_FALSE(OrganizationReady(prefs()));
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       TheProvidersRowOpensUwProviders) {
  OpenPage();
  content::WebContentsAddedObserver opened;
  ASSERT_EQ(true, Run("part('providersLink').click(); return true;"));
  content::WebContents* providers = opened.GetWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(providers));
  EXPECT_EQ(providers->GetLastCommittedURL(), GURL("chrome://providers/"));
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       ASwatchSetsTheWindowTintAndTheDefaultClearsIt) {
  constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);
  OpenPage();
  EXPECT_EQ(TextOf("tintStatus"),
            String(IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_DEFAULT));

  ASSERT_EQ(true,
            Run("const swatch = page().shadowRoot.querySelector("
                "    '[data-color=\"#5A1F3A\"]');"
                "swatch.scrollIntoView(); swatch.click();"
                "await until(() => text('tintStatus').includes('#5A1F3A'));"
                "return true;"));
  EXPECT_EQ(theme()->GetUserColor(), kWine);
  EXPECT_EQ(FrameColor(), kWine);
  EXPECT_EQ(TextOf("tintStatus"),
            l10n_util::GetStringFUTF8(
                IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_PICKED, u"#5A1F3A"));
  SaveWindowScreenshot(browser(), "settings-tint");

  ASSERT_EQ(true,
            Run(content::JsReplace(
                "part('defaultTint').click();"
                "await until(() => text('tintStatus') === $1);"
                "return true;",
                String(IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_DEFAULT))));
  EXPECT_FALSE(theme()->GetUserColor());
  EXPECT_EQ(FrameColor(), kDefaultLightTint);
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       AMidToneColorIsMadeReadableAndThePageSaysSo) {
  constexpr SkColor kRoyal = SkColorSetRGB(0x27, 0x47, 0xC7);
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  OpenPage();
  ASSERT_EQ(true,
            Run("const input = part('customTint'); input.value = '#2747c7';"
                "input.dispatchEvent(new Event('change'));"
                "await until(() => text('tintStatus').includes('#2747C7'));"
                "return true;"));

  const Tint tint = TintFrom(kRoyal);
  ASSERT_NE(tint.start, kRoyal);
  EXPECT_EQ(theme()->GetUserColor(), kRoyal);
  EXPECT_EQ(FrameColor(), tint.start);
  EXPECT_GE(color_utils::GetContrastRatio(tint.start, TextColorOn(tint)),
            kReadableContrast);
  EXPECT_EQ(TextOf("tintStatus"),
            l10n_util::GetStringFUTF8(
                IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_ADJUSTED, u"#2747C7",
                base::UTF8ToUTF16(Hex(tint.start))));
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       EachThresholdHasItsOwnSlider) {
  // Presses `key` on the slider `id` and returns the threshold then shown
  // beside it.
  const auto press = [this](const std::string& id, const std::string& key) {
    return Run(content::JsReplace(
        "const before = text($1 + 'Value');"
        "part($1).dispatchEvent(new KeyboardEvent('keydown', {key: $2}));"
        "await until(() => text($1 + 'Value') !== before);"
        "await page().updateComplete; return text($1 + 'Value');",
        id, key));
  };
  OpenPage();
  ASSERT_EQ(true,
            Run("await until(() => part('moveThreshold')); return true;"));
  EXPECT_EQ(TextOf("moveThresholdValue"), "0.70");
  EXPECT_EQ(TextOf("newTaskThresholdValue"), "0.55");
  EXPECT_EQ(TextOf("renameThresholdValue"), "0.70");
  EXPECT_EQ(4, Run("return part('moveThreshold').value;"));
  EXPECT_EQ(TextOf("thresholdsNote"),
            String(IDS_UW_SETTINGS_ORGANIZATION_THRESHOLDS_NOTE));

  EXPECT_EQ("0.95", press("moveThreshold", "End"));
  EXPECT_EQ("0.50", press("newTaskThreshold", "Home"));
  EXPECT_EQ("0.75", press("renameThreshold", "ArrowRight"));
  const Thresholds stored = ReadThresholds(prefs());
  EXPECT_EQ(stored.confidence, 0.95);
  EXPECT_EQ(stored.new_task, 0.50);
  EXPECT_EQ(stored.new_name, 0.75);
  SaveWindowScreenshot(browser(), "settings-thresholds");

  OpenPage();
  ASSERT_EQ(true,
            Run("await until(() => part('moveThreshold')); return true;"));
  EXPECT_EQ(TextOf("moveThresholdValue"), "0.95");
  EXPECT_EQ(TextOf("newTaskThresholdValue"), "0.50");
  EXPECT_EQ(TextOf("renameThresholdValue"), "0.75");
  EXPECT_EQ(9, Run("return part('moveThreshold').value;"));
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       PauseButtonsSetThePauseState) {
  OpenPage();
  ASSERT_EQ(true, Run("part('pauseIndefinite').click();"
                      "await until(() => part('pauseIndefinite').disabled);"
                      "return true;"));
  EXPECT_EQ(ReadPauseState(prefs()), PauseMode::kIndefinite);

  ASSERT_EQ(true, Run("part('resume').click();"
                      "await until(() => part('resume').disabled);"
                      "return true;"));
  EXPECT_EQ(ReadPauseState(prefs()), PauseMode::kOff);
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       TheCommandBarSwitchStartsOnAndTurnsItOff) {
  EXPECT_TRUE(prefs()->GetBoolean(kCommandBarEnabledPref));
  OpenPage();
  ASSERT_EQ(true, Run("const toggle = part('commandBarToggle');"
                      "await until(() => toggle.checked);"
                      "toggle.scrollIntoView(); toggle.click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !prefs()->GetBoolean(kCommandBarEnabledPref); }));
  SaveWindowScreenshot(browser(), "settings-command-bar");
}

}  // namespace
}  // namespace uw
