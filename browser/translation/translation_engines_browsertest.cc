// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/browser/translate_metrics_logger.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/translation/ai_translator.h"
#include "uw/browser/translation/google_cloud_translator.h"
#include "uw/browser/translation/page_translator.h"
#include "uw/browser/translation/translate_menu.h"
#include "uw/browser/translation/translation_engine.h"
#include "uw/browser/translation/translation_prefs.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kGoodKey[] = "good-key";
constexpr char kKeyRejected[] =
    "API key not valid. Please pass a valid API key.";

constexpr char kFrench[] = R"(<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><title>Une page</title></head>
<body>
<p id="first">Bonjour tout le monde. Voici une <a href="#">phrase</a> en
français, assez longue pour que la langue soit reconnue sans peine.</p>
</body></html>)";

constexpr char kFind[] = R"(
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
  const until = (selector) => new Promise((resolve) => {
    const check = () => {
      const element = find(document, selector);
      if (element) {
        resolve(element);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
  const untilText = (selector, text) => new Promise((resolve) => {
    const check = () => {
      const element = find(document, selector);
      if (element && element.textContent.includes(text)) {
        resolve(element.textContent.trim());
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
)";

// Plays Google Cloud Translation's v2 API: with the good key, each text comes
// back with "GC:" in front; with any other, Google's error.
std::unique_ptr<net::test_server::HttpResponse> Serve(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (request.relative_url == "/fr.html") {
    response->set_content_type("text/html; charset=utf-8");
    response->set_content(kFrench);
    return response;
  }
  if (!request.relative_url.starts_with("/translate/v2")) {
    return nullptr;
  }
  response->set_content_type("application/json");
  auto key = request.headers.find("X-goog-api-key");
  if (key == request.headers.end() || key->second != kGoodKey) {
    response->set_code(net::HTTP_BAD_REQUEST);
    response->set_content(
        base::StrCat({R"({"error": {"code": 400, "message": ")", kKeyRejected,
                      R"(", "status": "INVALID_ARGUMENT"}})"}));
    return response;
  }
  std::optional<base::DictValue> body =
      base::JSONReader::ReadDict(request.content, base::JSON_PARSE_RFC);
  base::ListValue translations;
  if (const base::ListValue* q = body ? body->FindList("q") : nullptr) {
    for (const base::Value& text : *q) {
      translations.Append(
          base::DictValue().Set("translatedText", "GC:" + text.GetString()));
    }
  }
  response->set_content(*base::WriteJson(base::DictValue().Set(
      "data", base::DictValue().Set("translations", std::move(translations)))));
  return response;
}

// Google Cloud Translation and the AI provider engine, against stand-ins for
// Google and a provider. See "Engines" in docs/plans/page-translation.md.
class TranslationEnginesBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    headless::HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    command_line->AppendSwitchASCII(
        "uw-google-cloud-translation-url",
        embedded_test_server()->GetURL("/translate/v2").spec());
  }

  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    embedded_test_server()->StartAcceptingConnections();
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  translate::TranslateManager* manager() {
    return ChromeTranslateClient::FromWebContents(contents())
        ->GetTranslateManager();
  }

  PrefService& prefs() { return *browser()->GetProfile()->GetPrefs(); }

  void Open() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("plain.test", "/fr.html")));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !manager()->GetLanguageState()->source_language().empty();
    }));
  }

  std::string First() {
    return content::EvalJs(contents(),
                           "document.querySelector('#first').textContent")
        .ExtractString();
  }

  std::optional<std::string> Check(const std::string& key) {
    base::test::TestFuture<std::optional<std::string>> checked;
    GoogleCloudTranslator::CheckKey(
        browser()
            ->GetProfile()
            ->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess(),
        key, checked.GetCallback());
    return checked.Get();
  }

  void SaveGoogleKey() {
    ASSERT_TRUE(
        ApiKeyStore().SetKey(GoogleCloudTranslator::kKeyAccount, kGoodKey));
    g_browser_process->local_state()->SetBoolean(
        kTranslationGoogleCloudKeySavedPref, true);
  }

  // A provider at `/v1/ai` with a language model for translating.
  void AddAiProvider() {
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Translator";
    provider.address = embedded_test_server()->GetURL("/v1/ai").spec();
    provider.format = ProviderFormat::kChatCompletions;
    provider.id = AddProvider(prefs(), provider);
    ModelChoice choice;
    choice.provider_id = provider.id;
    choice.model = "translate-model";
    SetTranslationModel(prefs(), choice);
  }

  std::string Settings(const std::string& script) {
    return content::EvalJs(contents(), std::string(kFind) + script)
        .ExtractString();
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
};

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       AKeyGoogleRejectsShowsGooglesWords) {
  EXPECT_EQ(kKeyRejected, Check("bad-key"));
  EXPECT_EQ(std::nullopt, Check(kGoodKey));
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       GoogleCloudTranslatesAPage) {
  SaveGoogleKey();
  SetTranslationEngine(prefs(), TranslationEngine::kGoogleCloud);
  EXPECT_TRUE(IsTranslationEngineSetUp(browser()->GetProfile(),
                                       TranslationEngine::kGoogleCloud));
  Open();
  manager()->TranslatePage(
      "fr", "en", /*triggered_from_menu=*/true,
      translate::TranslationType::kManualContextMenuInitialTranslation);
  EXPECT_TRUE(base::test::RunUntil([&] { return First().starts_with("GC:"); }));
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       TranslateWithAnAiProviderWorksOnce) {
  SaveGoogleKey();
  SetTranslationEngine(prefs(), TranslationEngine::kGoogleCloud);
  AddAiProvider();
  Open();
  // Two segments: the paragraph and the title.
  models_.Answer(
      "/v1/ai/chat/completions",
      R"({"choices": [{"message": {"role": "assistant", "content":)"
      R"( "[\"AI: Hello everyone. Here is a <a i=0>sentence</a> in French.\",)"
      R"( \"AI: A page\"]"}}]})");
  ExecuteTranslateMenuCommand(contents(), kTranslateWithAi);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return manager()->GetLanguageState()->IsPageTranslated() &&
           First() == "AI: Hello everyone. Here is a sentence in French.";
  }));
  EXPECT_EQ(TranslationEngine::kAi,
            PageTranslator::FromWebContents(contents())->engine());
  manager()->RevertTranslation();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !manager()->GetLanguageState()->IsPageTranslated(); }));
  manager()->TranslatePage(
      "fr", "en", /*triggered_from_menu=*/true,
      translate::TranslationType::kManualContextMenuReTranslation);
  EXPECT_TRUE(base::test::RunUntil([&] { return First().starts_with("GC:"); }));
  EXPECT_EQ(TranslationEngine::kGoogleCloud,
            PageTranslator::FromWebContents(contents())->engine());
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       AnAiProviderTranslatesAPage) {
  AddAiProvider();
  SetTranslationEngine(prefs(), TranslationEngine::kAi);
  EXPECT_TRUE(IsTranslationEngineSetUp(browser()->GetProfile(),
                                       TranslationEngine::kAi));
  Open();
  models_.Answer(
      "/v1/ai/chat/completions",
      R"({"choices": [{"message": {"role": "assistant", "content":)"
      R"( "```json\n[\"AI: Hello everyone. Here is a <a i=0>sentence</a>.\",)"
      R"( \"AI: A page\"]\n```"}}]})");
  manager()->TranslatePage(
      "fr", "en", /*triggered_from_menu=*/true,
      translate::TranslationType::kManualContextMenuInitialTranslation);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return First() == "AI: Hello everyone. Here is a sentence."; }));
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       SettingsSavesAKeyOnlyWhenGoogleTakesIt) {
  SetTranslationEngine(prefs(), TranslationEngine::kGoogleCloud);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/languages")));
  EXPECT_EQ(kKeyRejected, Settings(R"(
    (async () => {
      const key = await until('#uwKey');
      key.value = 'bad-key';
      key.dispatchEvent(new Event('input'));
      (await until('#uwSaveKey')).click();
      return untilText('#uwKeyNote', 'API key not valid');
    })())"));
  EXPECT_FALSE(g_browser_process->local_state()->GetBoolean(
      kTranslationGoogleCloudKeySavedPref));
  EXPECT_EQ("A key is saved, and it works.", Settings(R"(
    (async () => {
      const key = await until('#uwKey');
      key.value = 'good-key';
      key.dispatchEvent(new Event('input'));
      (await until('#uwSaveKey')).click();
      return untilText('#uwKeyNote', 'saved');
    })())"));
  EXPECT_TRUE(g_browser_process->local_state()->GetBoolean(
      kTranslationGoogleCloudKeySavedPref));
  EXPECT_EQ(kGoodKey, ApiKeyStore().GetKey(GoogleCloudTranslator::kKeyAccount));
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       TheModelMenuListsAProvidersModels) {
  AddAiProvider();
  models_.Answer("/v1/ai/models",
                 R"({"data": [{"id": "translate-model"}, {"id": "other"}]})");
  SetTranslationEngine(prefs(), TranslationEngine::kAi);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/languages")));
  EXPECT_EQ("translate-model · Translator", Settings(R"(
    (async () => {
      const menu = await until('#uwModel');
      await new Promise((resolve) => {
        const check = () => menu.options.length ? resolve() :
                                                  setTimeout(check, 20);
        check();
      });
      return menu.options[menu.selectedIndex].textContent.trim();
    })())"));
  Settings(R"(
    (async () => {
      const menu = await until('#uwModel');
      menu.value = String([...menu.options].findIndex(
          (option) => option.textContent.includes('other')));
      menu.dispatchEvent(new Event('change'));
      return '';
    })())");
  EXPECT_TRUE(base::test::RunUntil([&] {
    std::optional<ModelChoice> choice = ReadTranslationModel(prefs());
    return choice && choice->model == "other";
  }));
}

IN_PROC_BROWSER_TEST_F(TranslationEnginesBrowserTest,
                       TheMenuStartsOnTheNamingModelAtItsLowestEffort) {
  Provider provider = PresetProvider(ProviderPreset::kCustom);
  provider.name = "Namer";
  provider.address = embedded_test_server()->GetURL("/v1/ai").spec();
  provider.format = ProviderFormat::kChatCompletions;
  provider.id = AddProvider(prefs(), provider);
  SetChoice(prefs(), ModelSlot::kLanguage, provider.id, "namer-model");
  CatalogStore::SetSourceForTesting(
      embedded_test_server()->GetURL("/v1/catalog.json"));
  models_.Answer("/v1/ai/models", R"({"data": [{"id": "namer-model"}]})");
  models_.Answer(
      "/v1/catalog.json",
      base::StrCat(
          {R"({"namer": {"api": ")", provider.address,
           R"(", "models": {"namer-model": {"reasoning_options":)"
           R"( [{"type": "effort", "values": ["low", "high"]}]}}}})"}));
  SetTranslationEngine(prefs(), TranslationEngine::kAi);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/languages")));
  EXPECT_EQ("namer-model · Namer", Settings(R"(
    (async () => {
      const menu = await until('#uwModel');
      await new Promise((resolve) => {
        const check = () => menu.options.length ? resolve() :
                                                  setTimeout(check, 20);
        check();
      });
      return menu.options[menu.selectedIndex].textContent.trim();
    })())"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !prefs().GetDict(kTranslationModelPref).empty(); }));
  std::optional<ModelChoice> saved = ReadTranslationModel(prefs());
  ASSERT_TRUE(saved);
  EXPECT_EQ("namer-model", saved->model);
  EXPECT_EQ("low", saved->effort);
}

}  // namespace
}  // namespace uw
