// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/translate/translate_bubble_controller.h"
#include "chrome/browser/ui/views/translate/translate_bubble_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/context_menu_data/edit_flags.h"
#include "ui/base/models/menu_model.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "uw/browser/translation/translate_menu.h"
#include "uw/browser/translation/translation_engine.h"

namespace uw {
namespace {

constexpr char kSwahili[] = R"(<!doctype html>
<html lang="sw"><head><meta charset="utf-8"><title>Habari</title></head>
<body>
<p>Habari za asubuhi. Leo tunajifunza kuhusu historia ya Afrika Mashariki na
watu wake. Watoto wanacheza nje na wazazi wao wanafanya kazi shambani.</p>
</body></html>)";

std::unique_ptr<net::test_server::HttpResponse> Serve(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/sw.html") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html; charset=utf-8");
  response->set_content(kSwahili);
  return response;
}

template <typename T>
T* FindView(views::View* view, base::FunctionRef<bool(T*)> match) {
  if (auto* found = views::AsViewClass<T>(view); found && match(found)) {
    return found;
  }
  for (views::View* child : view->children()) {
    if (T* found = FindView<T>(child, match)) {
      return found;
    }
  }
  return nullptr;
}

// The ways in to page translation and its settings, per "Ways in" and
// "Settings" in docs/plans/page-translation.md.
class TranslationUiBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  translate::TranslateManager* manager() {
    return ChromeTranslateClient::FromWebContents(contents())
        ->GetTranslateManager();
  }

  void UseEngine(TranslationEngine engine) {
    SetTranslationEngine(*browser()->GetProfile()->GetPrefs(), engine);
  }

  void OpenSwahili() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("plain.test", "/sw.html")));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !manager()->GetLanguageState()->source_language().empty();
    }));
  }

  std::unique_ptr<TestRenderViewContextMenu> PageMenu() {
    content::ContextMenuParams params;
    params.page_url = contents()->GetLastCommittedURL();
    params.frame_url = params.page_url;
    params.edit_flags = blink::ContextMenuDataEditFlags::kCanTranslate;
    auto menu = std::make_unique<TestRenderViewContextMenu>(
        *contents()->GetPrimaryMainFrame(), params);
    menu->Init();
    return menu;
  }

  // Whether `command` is in `menu` or its submenus, and enabled and checked.
  struct Item {
    bool present = false;
    bool enabled = false;
    bool checked = false;
  };
  Item ItemOf(TestRenderViewContextMenu& menu, int command) {
    auto found = menu.GetMenuModelAndItemIndex(command);
    if (!found) {
      return Item();
    }
    auto [model, index] = *found;
    return Item{true, model->IsEnabledAt(index), model->IsItemCheckedAt(index)};
  }
};

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       WithNoEngineTheMenuOffersToSetUpTranslation) {
  OpenSwahili();
  std::unique_ptr<TestRenderViewContextMenu> menu = PageMenu();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_FALSE(menu->IsItemEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsItemPresent(kSetUpTranslation));
  EXPECT_TRUE(menu->IsItemEnabled(kSetUpTranslation));
  EXPECT_FALSE(ItemOf(*menu, kTranslateWithDevice).present);
}

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       WithAnEngineTheMenuListsTheEngines) {
  UseEngine(TranslationEngine::kDevice);
  OpenSwahili();
  std::unique_ptr<TestRenderViewContextMenu> menu = PageMenu();
  EXPECT_TRUE(menu->IsItemEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsItemPresent(kTranslateWith));
  Item device = ItemOf(*menu, kTranslateWithDevice);
  EXPECT_TRUE(device.enabled);
  EXPECT_TRUE(device.checked);
  EXPECT_TRUE(ItemOf(*menu, kTranslateWithGoogleCloud).present);
  EXPECT_FALSE(ItemOf(*menu, kTranslateWithGoogleCloud).enabled);
  EXPECT_FALSE(ItemOf(*menu, kTranslateWithAi).enabled);
  EXPECT_TRUE(ItemOf(*menu, kSetUpTranslation).present);
}

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       SetUpTranslationOpensTheLanguageSettings) {
  OpenSwahili();
  ui_test_utils::TabAddedWaiter added(browser());
  ExecuteTranslateMenuCommand(contents(), kSetUpTranslation);
  added.Wait();
  EXPECT_EQ(GURL("chrome://settings/languages"), contents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       NoBubblePopsUpOnAPageInAnotherLanguage) {
  UseEngine(TranslationEngine::kDevice);
  OpenSwahili();
  EXPECT_TRUE(manager()->GetLanguageState()->translate_enabled());
  EXPECT_FALSE(
      TranslateBubbleController::From(browser())->GetTranslateBubble());
}

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       TheBubbleNamesALanguageThisDeviceLacks) {
  UseEngine(TranslationEngine::kDevice);
  OpenSwahili();
  manager()->ShowTranslateUI(/*auto_translate=*/true,
                             /*triggered_from_menu=*/true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return manager()->GetLanguageState()->translation_error(); }));
  TranslateBubbleView* bubble = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    bubble = TranslateBubbleController::From(browser())->GetTranslateBubble();
    return bubble &&
           bubble->GetViewState() == TranslateBubbleModel::VIEW_STATE_ERROR;
  }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    views::Label* title = FindView<views::Label>(bubble, [](views::Label* l) {
      return l->GetProperty(views::kElementIdentifierKey) ==
             TranslateBubbleView::kErrorMessage;
    });
    return title && title->GetText() == u"This device can't translate Swahili";
  }));
  EXPECT_TRUE(
      FindView<views::MdTextButton>(bubble, [](views::MdTextButton* button) {
        return button->GetText() == u"Set up another engine…";
      }));
}

IN_PROC_BROWSER_TEST_F(TranslationUiBrowserTest,
                       TheSettingsPagePicksTheEngine) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/languages")));
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
  )";
  EXPECT_EQ("", content::EvalJs(contents(), std::string(kFind) + R"(
    (async () => {
      const engine = await until('#uwEngine');
      return engine.value;
    })())"));
  EXPECT_TRUE(content::ExecJs(contents(), std::string(kFind) + R"(
    (async () => {
      const engine = await until('#uwEngine');
      engine.value = 'device';
      engine.dispatchEvent(new Event('change'));
    })())"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return GetTranslationEngine(*browser()->GetProfile()->GetPrefs()) ==
           TranslationEngine::kDevice;
  }));
  EXPECT_EQ(true, content::EvalJs(contents(), std::string(kFind) + R"(
    (async () => (await until('#uwNoDownloads')).checkVisibility())())"));
}

}  // namespace
}  // namespace uw
