// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/translate/translate_controller.h"
#include "chrome/browser/ui/views/translate/partial_translate_bubble_view.h"
#include "chrome/browser/ui/views/translate/translate_bubble_controller.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/on_device_translation/features.h"
#include "components/on_device_translation/public/mojom/on_device_translation_service.mojom.h"
#include "components/on_device_translation/service/test/test_util.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/browser/translate_metrics_logger.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/process_type.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "uw/browser/translation/device_translator.h"
#include "uw/browser/translation/page_translator.h"
#include "uw/browser/translation/translation_engine.h"

namespace uw {
namespace {

// The mock TranslateKit translates only from English to Japanese, and puts
// the dictionary's text in front of each segment.
constexpr char kTranslated[] = "JA:";

constexpr char kEnglish[] = R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>An English page</title></head>
<body>
<p id="first">This is a <a href="#" id="link">link</a> in a sentence. Tokyo is
the capital of Japan, and every day many people take the train to work.</p>
<p id="second">Kyoto has many old temples and shrines, and in spring many
visitors come to see the cherry blossoms.</p>
<script>
window.clicks = 0;
document.getElementById('link').addEventListener('click', (event) => {
  event.preventDefault();
  window.clicks++;
});
</script>
</body></html>)";

constexpr char kSwahili[] = R"(<!doctype html>
<html lang="sw"><head><meta charset="utf-8"><title>Habari</title></head>
<body>
<p>Habari za asubuhi. Leo tunajifunza kuhusu historia ya Afrika Mashariki na
watu wake. Watoto wanacheza nje na wazazi wao wanafanya kazi shambani.</p>
</body></html>)";

std::unique_ptr<net::test_server::HttpResponse> Serve(
    const net::test_server::HttpRequest& request) {
  const char* page = request.relative_url == "/en.html"   ? kEnglish
                     : request.relative_url == "/sw.html" ? kSwahili
                                                          : nullptr;
  if (!page) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html; charset=utf-8");
  response->set_content(page);
  return response;
}

// Pages translated on this device, through uw's page script, per
// docs/plans/page-translation.md. A mock TranslateKit stands in for Google's.
class PageTranslationBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    headless::HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(packs_.CreateUniqueTempDir());
    const base::FilePath dictionary = packs_.GetPath().AppendASCII("en_ja");
    ASSERT_TRUE(base::CreateDirectory(dictionary));
    ASSERT_TRUE(
        base::WriteFile(dictionary.AppendASCII("dict.dat"), kTranslated));
    command_line->AppendSwitchPath(
        on_device_translation::kTranslateKitBinaryPath,
        on_device_translation::GetMockLibraryPath());
    command_line->AppendSwitchASCII("translate-kit-packages",
                                    "en,ja," + dictionary.AsUTF8Unsafe());
  }

  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* contents(Browser* in = nullptr) {
    return (in ? in : browser())->tab_strip_model()->GetActiveWebContents();
  }

  translate::TranslateManager* manager(Browser* in = nullptr) {
    return ChromeTranslateClient::FromWebContents(contents(in))
        ->GetTranslateManager();
  }

  void UseEngine(TranslationEngine engine) {
    SetTranslationEngine(*browser()->GetProfile()->GetPrefs(), engine);
  }

  // Opens `path` on a plain http site and waits for its language.
  void Open(const std::string& path, Browser* in = nullptr) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        in ? in : browser(),
        embedded_test_server()->GetURL("plain.test", path)));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !manager(in)->GetLanguageState()->source_language().empty();
    }));
  }

  // Translates the page into Japanese, the one language the mock knows.
  void Translate(Browser* in = nullptr) {
    manager(in)->TranslatePage(
        manager(in)->GetLanguageState()->source_language(), "ja",
        /*triggered_from_menu=*/true,
        translate::TranslationType::kManualContextMenuInitialTranslation);
  }

  std::string Text(const std::string& selector, Browser* in = nullptr) {
    return content::EvalJs(contents(in), "document.querySelector('" + selector +
                                             "').textContent")
        .ExtractString();
  }

  bool Translated(const std::string& selector, Browser* in = nullptr) {
    return Text(selector, in).starts_with(kTranslated);
  }

  std::string Html() {
    return content::EvalJs(contents(), "document.documentElement.outerHTML")
        .ExtractString();
  }

 private:
  base::ScopedTempDir packs_;
};

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       WithNoEngineNothingIsOfferedOrTranslated) {
  Open("/en.html");
  EXPECT_FALSE(ChromeTranslateClient::FromWebContents(contents())
                   ->HasTranslationEngine());
  EXPECT_FALSE(manager()->GetLanguageState()->translate_enabled());
  EXPECT_TRUE(manager()->CanManuallyTranslate());
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       ThisDeviceTranslatesAPlainHttpPage) {
  UseEngine(TranslationEngine::kDevice);
  Open("/en.html");
  EXPECT_FALSE(content::EvalJs(contents(), "isSecureContext").ExtractBool());
  Translate();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return manager()->GetLanguageState()->IsPageTranslated() &&
           Translated("#first") && Translated("#second");
  }));
  EXPECT_TRUE(Text("title").starts_with(kTranslated));
  EXPECT_EQ("link", Text("#link"));
  EXPECT_EQ(1, content::EvalJs(contents(), R"(
    document.getElementById('link').click();
    window.clicks)"));
  EXPECT_EQ(TranslationEngine::kDevice,
            PageTranslator::FromWebContents(contents())->engine());
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       ShowOriginalPutsThePageBackAsItWas) {
  UseEngine(TranslationEngine::kDevice);
  Open("/en.html");
  const std::string original = Html();
  Translate();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return manager()->GetLanguageState()->IsPageTranslated() &&
           Translated("#second");
  }));
  manager()->RevertTranslation();
  EXPECT_TRUE(base::test::RunUntil([&] { return Html() == original; }));
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       TextAddedLaterIsTranslatedToo) {
  UseEngine(TranslationEngine::kDevice);
  Open("/en.html");
  Translate();
  ASSERT_TRUE(base::test::RunUntil([&] { return Translated("#second"); }));
  ASSERT_TRUE(content::ExecJs(contents(), R"(
    const added = document.createElement('p');
    added.id = 'added';
    added.textContent = 'A paragraph added later.';
    document.body.appendChild(added);
    document.querySelector('#second').firstChild.data = 'Rewritten text.';
  )"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Translated("#added") &&
           Text("#second") == std::string(kTranslated) + "Rewritten text.";
  }));
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       ALanguageThisDeviceLacksIsNamed) {
  UseEngine(TranslationEngine::kDevice);
  Open("/sw.html");
  Translate();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return manager()->GetLanguageState()->translation_error(); }));
  const SegmentTranslator::Result& problem =
      PageTranslator::FromWebContents(contents())->problem();
  EXPECT_EQ(SegmentTranslator::Result::Problem::kUnsupportedLanguage,
            problem.problem);
  EXPECT_EQ("sw", problem.language);
  EXPECT_FALSE(Translated("p"));
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       ThisDeviceTranslatesASelectionAndLeavesThePage) {
  UseEngine(TranslationEngine::kDevice);
  Open("/en.html");
  const std::string first = Text("#first");
  TranslateController::From(browser())->StartPartialTranslate(
      "en", "ja", u"Tea & coffee.\n\nKyoto has temples.");
  EXPECT_TRUE(base::test::RunUntil([&] {
    PartialTranslateBubbleView* bubble =
        TranslateBubbleController::From(browser())->GetPartialTranslateBubble();
    return bubble && bubble->model()->GetTargetText() ==
                         u"JA:Tea & coffee.\n\nJA:Kyoto has temples.";
  }));
  EXPECT_FALSE(manager()->GetLanguageState()->IsPageTranslated());
  EXPECT_EQ(first, Text("#first"));
}

// Chromium ends the engine's service when a download changes what is
// installed, which can cut off a batch on its way to the engine.
IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       ABatchTheServiceCutsOffGoesToANewTranslator) {
  DeviceTranslator translator(browser()->GetProfile(), base::DoNothing());
  base::test::TestFuture<SegmentTranslator::Result> first;
  translator.Translate({"Hello"}, "en", "ja", first.GetCallback());
  ASSERT_EQ(std::vector<std::string>{"JA:Hello"}, first.Get().translations);
  int ended = 0;
  for (content::BrowserChildProcessHostIterator it(
           content::PROCESS_TYPE_UTILITY);
       !it.Done(); ++it) {
    if (it.GetData().metrics_name ==
        on_device_translation::mojom::OnDeviceTranslationService::Name_) {
      it.GetData().GetProcess().Terminate(0, /*wait=*/false);
      ++ended;
    }
  }
  ASSERT_EQ(1, ended);
  // Sent in the same task, before the translator hears its service ended.
  base::test::TestFuture<SegmentTranslator::Result> second;
  translator.Translate({"Again"}, "en", "ja", second.GetCallback());
  EXPECT_EQ(SegmentTranslator::Result::Problem::kNone, second.Get().problem);
  EXPECT_EQ(std::vector<std::string>{"JA:Again"}, second.Get().translations);
}

IN_PROC_BROWSER_TEST_F(PageTranslationBrowserTest,
                       APrivateWindowTranslatesWithTheProfilesEngine) {
  UseEngine(TranslationEngine::kDevice);
  Browser* incognito = CreateIncognitoBrowser();
  Open("/en.html", incognito);
  Translate(incognito);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Translated("#first", incognito); }));
}

}  // namespace
}  // namespace uw
