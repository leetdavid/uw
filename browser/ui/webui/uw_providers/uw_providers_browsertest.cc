// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

constexpr char kHelpers[] = R"(
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
  const all = (selector, root = document) =>
      [...root.querySelectorAll(selector)];
  const withText = (selector, text, root = document) =>
      all(selector, root).find((element) =>
          element.textContent.trim() === text);
  const dialog = () => document.querySelector('[role="dialog"]');
  // A field by the start of its label: Fluid draws the label twice, the
  // second copy hidden.
  const field = (label) => all('input').find((input) =>
      input.labels[0]?.textContent.startsWith(label));
  // React keeps its own copy of a field's value; the native setter and an
  // input event reach it.
  const type = (label, value) => {
    const input = field(label);
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
        .set.call(input, value);
    input.dispatchEvent(new Event('input', {bubbles: true}));
  };
  const item = (name, root = document) => all('[role^="menuitem"]', root)
      .find((row) => row.getAttribute('aria-label') === name);
  const chip = (slot) =>
      document.querySelector(`[aria-label="${slot}"] [aria-haspopup="menu"]`);
  const openMenu = async (slot) => {
    chip(slot).dispatchEvent(new PointerEvent('pointerdown',
        {bubbles: true, button: 0, pointerType: 'mouse'}));
    return until(() => document.querySelector('[role="menu"]'));
  };
  const testLine = (slot) =>
      document.querySelector(`[aria-label="${slot}"] [role="status"]`);
  // A model's row on its provider's page, as text.
  const modelRow = (name) =>
      withText('span', name)?.closest('.grid')?.textContent;
)";

constexpr char kTwoModels[] = R"({"data":[{"id":"chat-a"},{"id":"chat-b"}]})";

// models.dev's catalog, cut down, and an OpenRouter list of its two models.
constexpr char kCatalog[] = R"({"openrouter": {"models": {
  "maker/thinker": {"tool_call": true, "reasoning_options":
                        [{"type": "effort", "values": ["low", "high"]}]},
  "maker/talker": {"tool_call": false}}}})";
constexpr char kCatalogModels[] =
    R"({"data":[{"id":"maker/thinker"},{"id":"maker/talker"}]})";

// Drives the real uw://providers page against a local server that plays the
// providers.
class UwProvidersBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    CatalogStore::SetSourceForTesting(
        embedded_test_server()->GetURL("/v1/catalog.json"));
  }

  PrefService& prefs() { return *browser()->GetProfile()->GetPrefs(); }
  content::WebContents* page() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  std::string Address() { return embedded_test_server()->GetURL("/v1").spec(); }

  // Saves `provider` with a key, as the page saves one, and returns its id.
  std::string Save(Provider provider) {
    provider.id = AddProvider(prefs(), provider);
    provider.key_account = KeyAccountFor(provider.id);
    EXPECT_TRUE(ApiKeyStore().SetKey(provider.key_account, "sk-test"));
    UpdateProvider(prefs(), provider);
    return provider.id;
  }

  // A Custom provider at the test server.
  std::string AddTestServer(ProviderFormat format) {
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Test server";
    provider.address = Address();
    provider.format = format;
    return Save(std::move(provider));
  }

  // A provider made from `preset`, played by the test server at `address`.
  std::string AddPreset(ProviderPreset preset, const std::string& address) {
    Provider provider = PresetProvider(preset);
    provider.address = address;
    return Save(std::move(provider));
  }

  // Opens uw://providers, or one provider's page, once it has drawn.
  void Open(const std::string& id = "") {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://providers/" + id)));
    ASSERT_EQ(true, Run("await until(() => document.querySelector('h1'));"
                        "return true;"));
  }

  // Runs `body` as the body of an async function inside the page.
  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        page(), base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  // Picks `model` in the menu of the `slot` chip, such as "Language model".
  void Pick(const std::string& slot, const std::string& model) {
    ASSERT_EQ(true, Run(content::JsReplace(
                        "const menu = await openMenu($1);"
                        "(await until(() => item($2, menu))).click();"
                        "await until(() => chip($1).textContent.includes($2));"
                        "return true;",
                        slot, model)));
  }

  // Clicks the Test button of `slot` and returns the line it writes.
  std::string Test(const std::string& slot) {
    return Run(content::JsReplace(
                   "const row = document.querySelector('[aria-label=\"' + $1 "
                   "    + '\"]');"
                   "withText('button', 'Test', row).click();"
                   "return await until(() => {"
                   "  const text = testLine($1)?.textContent ?? '';"
                   "  return /[✓✕]/.test(text) && text;"
                   "});",
                   slot))
        .ExtractString();
  }

  std::vector<std::string> RequestsTo(const std::string& path) {
    std::vector<std::string> found;
    for (const ModelRequest& request : models_.Requests()) {
      if (request.path == path) {
        found.push_back(request.method + " " + request.authorization);
      }
    }
    return found;
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
};

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ACustomProviderIsAddedOnceItsModelsAreListed) {
  models_.Answer("/v1/models",
                 R"({"data":[{"id":"chat-a"},{"id":"text-embedding-3"}]})");
  Open();

  EXPECT_EQ(true, Run(content::JsReplace(
                      "withText('button', 'Add provider').click();"
                      "await until(dialog);"
                      "withText('button', 'Custom', dialog()).click();"
                      "await until(() => field('Address'));"
                      "type('Name', 'Test server');"
                      "type('Address', $1);"
                      "type('Key (not needed for a local address)', "
                      "    'sk-test');"
                      "withText('button', 'Add provider', dialog()).click();"
                      "await until(() => !dialog());"
                      "await until(() => withText('a *', '1 model'));"
                      "return true;",
                      Address())));

  const std::optional<Provider> saved = FindProvider(prefs(), "custom");
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->name, "Test server");
  EXPECT_EQ(saved->address, Address());
  EXPECT_EQ(saved->format, ProviderFormat::kChatCompletions);
  EXPECT_EQ(ApiKeyStore().GetKey(saved->key_account), "sk-test");
  // Once to check the address, once for the list.
  EXPECT_THAT(RequestsTo("/v1/models"),
              ElementsAre("GET Bearer sk-test", "GET Bearer sk-test"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AnAddressThatListsNoModelsIsNotAdded) {
  models_.Fail("/v1/models", 401, R"({"error":{"message":"Bad key."}})");
  Open();

  EXPECT_THAT(Run(content::JsReplace(
                      "withText('button', 'Add provider').click();"
                      "await until(dialog);"
                      "withText('button', 'Custom', dialog()).click();"
                      "await until(() => field('Address'));"
                      "type('Name', 'Test server');"
                      "type('Address', $1);"
                      "withText('button', 'Add provider', dialog()).click();"
                      "return (await until(() =>"
                      "    dialog().querySelector('[role=\"alert\"]')))"
                      "    .textContent;",
                      Address()))
                  .ExtractString(),
              HasSubstr("HTTP 401 Bad key."));
  EXPECT_TRUE(ReadProviders(prefs()).empty());
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ALanguageModelPassesItsTestWhenItAnswers) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/chat/completions", ChatAnswer("Hello"));
  Open();

  Pick("Language model", "chat-b");
  std::optional<ModelChoice> choice = ReadChoice(prefs(), ModelSlot::kLanguage);
  ASSERT_TRUE(choice);
  EXPECT_EQ(choice->provider_id, id);
  EXPECT_EQ(choice->model, "chat-b");
  // Picking sends nothing; only Test does.
  EXPECT_THAT(RequestsTo("/v1/chat/completions"), IsEmpty());

  EXPECT_THAT(Test("Language model"), HasSubstr("chat-b answered in"));
  EXPECT_THAT(RequestsTo("/v1/chat/completions"),
              ElementsAre("POST Bearer sk-test"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ALanguageModelThatSendsNoTextFailsItsTest) {
  AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/chat/completions", ChatAnswer(""));
  Open();

  Pick("Language model", "chat-a");
  EXPECT_THAT(Test("Language model"),
              HasSubstr("chat-a answered, but not with text."));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ADecisionModelPassesWhenItAnswersWithAChoice) {
  AddTestServer(ProviderFormat::kOpenAiDecisions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/decisions", DecisionsAnswer("none", 0.9));
  Open();

  // A provider that only decides offers no language model.
  EXPECT_EQ(true, Run("const menu = await openMenu('Language model');"
                      "await until(() => menu.textContent.includes("
                      "    'Add a provider first'));"
                      "document.dispatchEvent(new KeyboardEvent('keydown',"
                      "    {key: 'Escape', bubbles: true}));"
                      "await until(() => !document.querySelector("
                      "    '[role=\"menu\"]'));"
                      "return true;"));
  Pick("Decision model", "chat-a");
  EXPECT_THAT(Test("Decision model"), HasSubstr("chat-a answered in"));
  EXPECT_THAT(RequestsTo("/v1/decisions"), ElementsAre("POST Bearer sk-test"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       RemovingAProviderTakesItsKeyAndLeavesTheChoice) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  SetChoice(prefs(), ModelSlot::kLanguage, id, "chat-a");
  models_.Answer("/v1/models", kTwoModels);
  Open(id);

  content::TestNavigationObserver back_to_list(page());
  EXPECT_EQ(true, Run("await until(() => withText('h1', 'Test server'));"
                      "await until(() => withText('span', 'Language model'));"
                      "withText('button', 'Remove provider').click();"
                      "return true;"));
  back_to_list.Wait();
  EXPECT_EQ(page()->GetLastCommittedURL(), GURL("chrome://providers/"));
  EXPECT_FALSE(FindProvider(prefs(), id));
  EXPECT_FALSE(ApiKeyStore().GetKey(KeyAccountFor(id)));
  EXPECT_EQ(ReadChoice(prefs(), ModelSlot::kLanguage)->model, "chat-a");
}

// A provider that could not list its models without a key lists them again
// once a key is saved on its page, without a reload.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, ANewKeyListsTheModelsAgain) {
  Provider provider = PresetProvider(ProviderPreset::kCustom);
  provider.name = "Test server";
  provider.address = Address();
  const std::string id = AddProvider(prefs(), std::move(provider));
  models_.Fail("/v1/models", 401, R"({"error":{"message":"Bad key."}})");
  Open(id);
  EXPECT_EQ(true, Run("await until(() => document.body.innerText"
                      "    .includes('HTTP 401 Bad key.'));"
                      "return true;"));

  models_.Answer("/v1/models", kTwoModels);
  EXPECT_EQ(true, Run("withText('button', 'Add a key').click();"
                      "await until(() => field('New key'));"
                      "type('New key', 'sk-new');"
                      "await until(() => !withText('button', 'Save').disabled);"
                      "withText('button', 'Save').click();"
                      "await until(() => modelRow('chat-b'));"
                      "return true;"));
  EXPECT_THAT(RequestsTo("/v1/models"),
              ElementsAre("GET ", "GET Bearer sk-new"));
}

// The page is a React bundle under WebUI's script rules, which refuse HTML
// strings (Trusted Types) and scripts from elsewhere. A broken rule shows as
// a console error.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ThePageRunsWithoutConsoleErrors) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  SetChoice(prefs(), ModelSlot::kLanguage, id, "chat-a");
  models_.Answer("/v1/models", kTwoModels);
  content::WebContentsConsoleObserver console(page());
  console.SetFilter(base::BindRepeating(
      [](const content::WebContentsConsoleObserver::Message& message) {
        return message.log_level == blink::mojom::ConsoleMessageLevel::kError;
      }));
  Open();
  EXPECT_EQ(true, Run("await until(() => withText('a *', '2 models'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "providers");

  EXPECT_EQ(true, Run("await openMenu('Agent model');"
                      "await until(() => item('chat-b'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "providers-menu");

  Open(id);
  EXPECT_EQ(true, Run("await until(() => modelRow('chat-b'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "provider");
  EXPECT_TRUE(console.messages().empty()) << console.GetMessageAt(0);
}

// The settings sidebar lists AI providers under uw, like Extensions, and
// opens the page in a new tab.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       TheSettingsSidebarOpensAiProviders) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/")));
  content::WebContentsAddedObserver opened;
  EXPECT_EQ("chrome://providers/",
            Run("const find = (root) => root.querySelector('#uwProvidersLink')"
                "    ?? all('*', root).map((element) => element.shadowRoot &&"
                "        find(element.shadowRoot)).find(Boolean);"
                "const link = await until(() => find(document));"
                "link.click();"
                "return link.getAttribute('href');"));
  content::WebContents* providers = opened.GetWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(providers));
  EXPECT_EQ(providers->GetLastCommittedURL(), GURL("chrome://providers/"));
}

// Anthropic takes its key in its own header, for the model list and for the
// test alike, and the test goes over Anthropic Messages.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AnAnthropicModelIsListedAndTestedInItsOwnFormat) {
  AddTestServer(ProviderFormat::kAnthropicMessages);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/messages", R"({"stop_reason":"end_turn","content":[)"
                                 R"({"type":"text","text":"Hello"}]})");
  Open();

  Pick("Language model", "chat-a");
  EXPECT_THAT(Test("Language model"), HasSubstr("chat-a answered in"));

  std::vector<std::string> keys;
  for (const ModelRequest& request : models_.Requests()) {
    keys.push_back(request.method + " " + request.path + " " +
                   request.anthropic_key + request.authorization);
  }
  EXPECT_THAT(
      keys, ElementsAre("GET /v1/models sk-test", "POST /v1/messages sk-test"));
}

// The provider's page says what the catalog knows of each model, and a
// model that calls no tools cannot run chats.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       TheCatalogSaysWhatEachModelCanDo) {
  const std::string id = AddPreset(ProviderPreset::kOpenRouter, Address());
  models_.Answer("/v1/models", kCatalogModels);
  models_.Answer("/v1/catalog.json", kCatalog);
  Open(id);
  EXPECT_EQ("maker/thinkerCalls toolslowhigh",
            Run("return await until(() => modelRow('maker/thinker'));"));
  EXPECT_EQ("maker/talkerNo tool callsNo effort levels",
            Run("return await until(() => modelRow('maker/talker'));"));

  Open();
  EXPECT_EQ("No tool calls|",
            Run("const menu = await openMenu('Agent model');"
                "const note = (name) => item(name, menu)?.dataset.note ?? '';"
                "await until(() => item('maker/talker', menu));"
                "return note('maker/talker') + '|' + note('maker/thinker');"));
}

// An effort picked for the language model goes with its requests, in the
// field its provider reads.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, AnEffortGoesWithTheModel) {
  AddPreset(ProviderPreset::kOpenRouter, Address());
  models_.Answer("/v1/models", kCatalogModels);
  models_.Answer("/v1/catalog.json", kCatalog);
  models_.Answer("/v1/chat/completions", ChatAnswer("Hello"));
  Open();
  Pick("Language model", "maker/thinker");
  ASSERT_EQ(
      true,
      Run("const effort = () => document.querySelector("
          "    '[aria-label=\"Language model\"] [aria-label=\"Effort\"]');"
          "(await until(effort)).dispatchEvent(new PointerEvent("
          "    'pointerdown', {bubbles: true, button: 0, pointerType: "
          "'mouse'}));"
          "(await until(() => all('[role=\"menu\"]')"
          "    .map((menu) => item('high', menu)).find(Boolean)))"
          "    .click();"
          "await until(() => effort().textContent.includes('high'));"
          "return true;"));
  EXPECT_EQ(ReadChoice(prefs(), ModelSlot::kLanguage)->effort, "high");

  EXPECT_THAT(Test("Language model"), HasSubstr("maker/thinker answered in"));
  std::string sent;
  for (const ModelRequest& request : models_.Requests()) {
    if (request.path == "/v1/chat/completions") {
      sent = request.body;
    }
  }
  EXPECT_THAT(sent, HasSubstr(R"("reasoning":{"effort":"high"})"));
}

// A preset already added says so under its name, and its tile grows to fit,
// even where text renders taller than here.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, AnAddedPresetsTileHoldsItsIcon) {
  AddPreset(ProviderPreset::kJev, Address());
  models_.Answer("/v1/models", kTwoModels);
  Open();
  EXPECT_EQ(
      "Jev Added fits",
      Run("withText('button', 'Add provider').click();"
          "await until(dialog);"
          "const tall = document.createElement('style');"
          "tall.textContent = '[role=dialog] button * {line-height: 2}';"
          "document.head.append(tall);"
          "const tile = await until(() => all('button', dialog())"
          "    .find((button) => button.textContent === 'JevAdded'));"
          "await new Promise((resolve) => requestAnimationFrame(resolve));"
          "const icon = tile.firstElementChild.getBoundingClientRect();"
          "const box = tile.getBoundingClientRect();"
          "return 'Jev Added ' + (icon.top >= box.top &&"
          "    tile.scrollHeight <= tile.clientHeight ? 'fits' :"
          "    'spills: ' + [icon.top, box.top, tile.scrollHeight,"
          "                        tile.clientHeight]);"));
  SaveWindowScreenshot(browser(), "add-provider");
}

// Ollama says itself whether a model calls tools and thinks.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, OllamaSaysWhatItsModelsCanDo) {
  const std::string id =
      AddPreset(ProviderPreset::kOllama,
                embedded_test_server()->GetURL("/v1/ollama/v1").spec());
  models_.Answer("/v1/ollama/v1/models", R"({"data":[{"id":"qwen3"}]})");
  models_.Answer("/v1/ollama/api/show",
                 R"({"capabilities":["completion","thinking"]})");
  Open(id);
  EXPECT_EQ("qwen3No tool callslowmediumhigh",
            Run("return await until(() => modelRow('qwen3'));"));
  EXPECT_THAT(RequestsTo("/v1/catalog.json"), IsEmpty());
}

}  // namespace
}  // namespace uw
