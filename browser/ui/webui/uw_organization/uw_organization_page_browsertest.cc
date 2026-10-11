// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
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
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/model_test_server.h"
#include "uw/browser/tab_organization/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/components/look/tint.h"

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

constexpr char kChatDecision[] =
    R"({\"decision\":\"leave\",\"confidence\":0.9})";

// Drives the real settings page against a local server that plays the model
// providers.
class UwOrganizationPageBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  std::string Endpoint() {
    return embedded_test_server()->GetURL("/v1").spec();
  }

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
    // The endpoint is filled in once the page has loaded the saved state.
    ASSERT_EQ(true, Run("await until(() => part('languageEndpoint')?.value);"
                        "return true;"));
  }

  template <typename Kind>
  static std::string Number(Kind kind) {
    return base::NumberToString(static_cast<int>(kind));
  }

  void Choose(const std::string& id, const std::string& value) {
    ASSERT_EQ(true, Run(content::JsReplace(
                        "const select = part($1); select.value = $2;"
                        "select.dispatchEvent(new Event('change'));"
                        "await page().updateComplete; return true;",
                        id, value)));
  }

  void Type(const std::string& id, const std::string& value) {
    ASSERT_EQ(true, Run(content::JsReplace(
                        "part($1).value = $2;"
                        "await part($1).updateComplete;"
                        "await page().updateComplete; return true;",
                        id, value)));
  }

  std::string ValueOf(const std::string& id) {
    return Run(content::JsReplace("return part($1).value;", id))
        .ExtractString();
  }

  std::string TextOf(const std::string& id) {
    return Run(content::JsReplace("return text($1);", id)).ExtractString();
  }

  // Clicks `button` and returns the text of `status` once it stops showing
  // `busy`.
  std::string ClickAndWait(const std::string& button,
                           const std::string& status,
                           int busy) {
    return Run(content::JsReplace(
                   "part($1).click(); await page().updateComplete;"
                   "return await until(() => text($2) !== $3 && text($2));",
                   button, status, String(busy)))
        .ExtractString();
  }

  std::string Save() {
    return ClickAndWait("saveButton", "saveStatus",
                        IDS_UW_SETTINGS_ORGANIZATION_CHECKING);
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

  std::vector<std::string> RequestPaths() {
    std::vector<std::string> paths;
    for (const ModelRequest& request : models_.Requests()) {
      paths.push_back(request.method + " " + request.path);
    }
    return paths;
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
};

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       JevAndALanguageModelValidate) {
  models_.Answer("/v1/systemone", SystemOneAnswer("scratchpad", 0.9));
  models_.Answer("/v1/chat/completions", ChatAnswer("Check connection"));
  OpenPage();
  EXPECT_EQ(TextOf("status"), String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_OFF));

  Choose("decisionKind", Number(DecisionProvider::kJev));
  EXPECT_EQ(ValueOf("decisionEndpoint"), kJevDefaultEndpoint);
  EXPECT_EQ(ValueOf("decisionModel"), kJevDefaultModel);
  Type("decisionEndpoint", Endpoint());
  Type("decisionKey", "jev-key");
  EXPECT_EQ(ValueOf("languageEndpoint"), kOllamaDefaultEndpoint);
  Type("languageEndpoint", Endpoint());
  Type("languageModel", "llama3.2");

  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  EXPECT_TRUE(OrganizationReady(prefs()));
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs());
  EXPECT_EQ(decision.provider, DecisionProvider::kJev);
  EXPECT_EQ(decision.endpoint, Endpoint());
  EXPECT_EQ(decision.model, kJevDefaultModel);
  EXPECT_EQ(ApiKeyStore().GetKey(kDecisionKeyAccount), "jev-key");
  EXPECT_EQ(prefs()->GetString(kDecisionEndpointPref).find("jev-key"),
            std::string::npos);
  EXPECT_EQ(ValueOf("decisionKey"), "");

  const std::vector<ModelRequest> requests = models_.Requests();
  ASSERT_EQ(requests.size(), 2u);
  for (const ModelRequest& request : requests) {
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.authorization,
              request.path == "/v1/systemone" ? "Bearer jev-key" : "");
  }
  EXPECT_EQ(TextOf("status"), String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_ON));
  EXPECT_EQ(TextOf("statusDetail"),
            "Jev decides where tabs go. llama3.2 names new tasks.");
  SaveWindowScreenshot(browser(), "settings");
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       OpenAiDecisionsValidatesWithoutALanguageModel) {
  models_.Answer("/v1/decisions", DecisionsAnswer("scratchpad", 0.9));
  OpenPage();
  Choose("decisionKind", Number(DecisionProvider::kOpenAi));
  EXPECT_EQ(ValueOf("decisionEndpoint"), kOpenAiDefaultEndpoint);
  EXPECT_EQ(ValueOf("decisionModel"), kOpenAiDecisionsDefaultModel);
  Type("decisionEndpoint", Endpoint());
  Type("decisionKey", "openai-key");

  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  EXPECT_TRUE(OrganizationReady(prefs()));
  EXPECT_EQ(ReadDecisionModelConfig(prefs()).provider,
            DecisionProvider::kOpenAi);
  EXPECT_EQ(RequestPaths(), std::vector<std::string>{"POST /v1/decisions"});
  EXPECT_EQ(TextOf("statusDetail"),
            "OpenAI Decisions API decides where tabs go.");
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       OpenAiCompatibleDecisionModelValidates) {
  models_.Answer("/v1/decisions", DecisionsAnswer("new_task", 0.7));
  OpenPage();
  Choose("decisionKind", Number(DecisionProvider::kOpenAiCompatible));
  EXPECT_EQ(ValueOf("decisionEndpoint"), "");
  Type("decisionEndpoint", Endpoint());
  Type("decisionModel", "house-decider");
  Type("decisionKey", "house-key");

  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs());
  EXPECT_EQ(decision.provider, DecisionProvider::kOpenAiCompatible);
  EXPECT_EQ(decision.model, "house-decider");
  const std::vector<ModelRequest> requests = models_.Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].authorization, "Bearer house-key");
  EXPECT_NE(requests[0].body.find("house-decider"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       LanguageModelAloneDecidesAndNames) {
  models_.Answer("/v1/chat/completions", ChatAnswer(kChatDecision));
  OpenPage();
  Choose("languageKind", Number(ConnectionMode::kOpenAiCompatible));
  Type("languageEndpoint", Endpoint());
  Type("languageModel", "gpt-house");
  Type("languageKey", "chat-key");

  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  EXPECT_TRUE(OrganizationReady(prefs()));
  const std::vector<ModelRequest> requests = models_.Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].path, "/v1/chat/completions");
  EXPECT_EQ(requests[0].authorization, "Bearer chat-key");
  EXPECT_EQ(TextOf("statusDetail"),
            "gpt-house decides where tabs go and names new tasks.");
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       AFailedSaveTurnsOrganizationOff) {
  models_.Answer("/v1/decisions", DecisionsAnswer("scratchpad", 0.9));
  OpenPage();
  Choose("decisionKind", Number(DecisionProvider::kOpenAiCompatible));
  Type("decisionEndpoint", Endpoint());
  Type("decisionModel", "house-decider");
  Type("decisionKey", "house-key");
  ASSERT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  ASSERT_TRUE(OrganizationReady(prefs()));

  models_.Answer("/v1/decisions", R"({"answers":[{"type":"refusal"}]})");
  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_DECISION_ERROR));
  EXPECT_FALSE(OrganizationReady(prefs()));
  EXPECT_EQ(TextOf("status"), String(IDS_UW_SETTINGS_ORGANIZATION_STATUS_OFF));

  Type("languageEndpoint", Endpoint());
  Type("languageModel", "missing-model");
  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_DECISION_ERROR) + " " +
                        String(IDS_UW_SETTINGS_ORGANIZATION_LANGUAGE_ERROR));
  EXPECT_FALSE(OrganizationReady(prefs()));
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       NothingSetCannotTurnOrganizationOn) {
  OpenPage();
  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_NOTHING_SET));
  EXPECT_FALSE(OrganizationReady(prefs()));
  EXPECT_TRUE(models_.Requests().empty());
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       ModelListLoadsWithGetAndFillsTheModel) {
  models_.Answer("/v1/models",
                 R"({"data":[{"id":"llama3.2"},{"id":"qwen3"}]})");
  models_.Answer("/v1/chat/completions", ChatAnswer(kChatDecision));
  OpenPage();
  Type("languageEndpoint", Endpoint());
  EXPECT_EQ(ClickAndWait("languageLoadModels", "languageModelsStatus",
                         IDS_UW_SETTINGS_ORGANIZATION_LOADING),
            "Models found: 2");
  EXPECT_EQ(RequestPaths(), std::vector<std::string>{"GET /v1/models"});
  EXPECT_EQ(ValueOf("languageModelSelect"), "llama3.2");

  Choose("languageModelSelect", "qwen3");
  EXPECT_EQ(Save(), String(IDS_UW_SETTINGS_ORGANIZATION_VALIDATED));
  EXPECT_EQ(ReadAiConnectionConfig(prefs()).model, "qwen3");
}

IN_PROC_BROWSER_TEST_F(UwOrganizationPageBrowserTest,
                       JevModelListLoadsInTypeSafesShape) {
  models_.Answer("/v1/models",
                 R"({"models":[{"name":"jev-latest","description":"Latest"},)"
                 R"({"name":"jev-preview","description":"Preview"}]})");
  OpenPage();
  Choose("decisionKind", Number(DecisionProvider::kJev));
  Type("decisionEndpoint", Endpoint());
  Type("decisionKey", "jev-key");
  EXPECT_EQ(ClickAndWait("decisionLoadModels", "decisionModelsStatus",
                         IDS_UW_SETTINGS_ORGANIZATION_LOADING),
            "Models found: 2");
  EXPECT_EQ(ValueOf("decisionModelSelect"), "jev-latest");

  const std::vector<ModelRequest> requests = models_.Requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].method, "GET");
  EXPECT_EQ(requests[0].authorization, "Bearer jev-key");
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

}  // namespace
}  // namespace uw
