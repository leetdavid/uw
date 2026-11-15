// Copyright 2026 The uw Authors. All rights reserved.

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/base64url.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/platform_util_internal.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/memory_store.h"
#include "uw/browser/agent/skill_folders.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/decision_probes.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/plan_sign_in.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/connectors/connector_catalog_store.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::StartsWith;

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

// What uw asks a model to learn whether it decides, and a decision model's
// answer.
constexpr char kProbe[] = "uw checks that this model can answer.";
constexpr char kProbeAnswer[] =
    R"({"answers":[{"type":"choice","name":"answer","choice":"yes",)"
    R"("confidence":0.97}]})";

// Opens the menu of `slot`, finds `shown` in it and not `hidden`, and closes
// it.
constexpr char kOnlyIn[] = R"(
  const onlyIn = async (slot, shown, hidden) => {
    const menu = await openMenu(slot);
    await until(() => item(shown, menu));
    const found = !item(hidden, menu);
    document.dispatchEvent(new KeyboardEvent('keydown',
        {key: 'Escape', bubbles: true}));
    await until(() => !document.querySelector('[role="menu"]'));
    return found;
  };
  // The models the quick choices list under "Decision models", once `shown`
  // is there, joined with spaces.
  const decisionModels = async (shown) => {
    const menu = await openMenu('Makes quick choices');
    await until(() => item(shown, menu));
    const languages = all('div', menu).find((label) =>
        label.textContent.startsWith('Or a language model'));
    const names = all('[role^="menuitem"]', menu)
        .filter((row) => row.compareDocumentPosition(languages) &
                         Node.DOCUMENT_POSITION_FOLLOWING)
        .map((row) => row.getAttribute('aria-label'));
    document.dispatchEvent(new KeyboardEvent('keydown',
        {key: 'Escape', bubbles: true}));
    await until(() => !document.querySelector('[role="menu"]'));
    return names.join(' ');
  };
)";

// models.dev's catalog, cut down, and an OpenRouter list of its two models.
constexpr char kCatalog[] = R"({"openrouter": {"models": {
  "maker/thinker": {"tool_call": true, "reasoning_options":
                        [{"type": "effort", "values": ["low", "high"]}]},
  "maker/talker": {"tool_call": false}}}})";
constexpr char kCatalogModels[] =
    R"({"data":[{"id":"maker/thinker"},{"id":"maker/talker"}]})";

// A ChatGPT plan's model list and a streamed answer, as Codex gets them.
constexpr char kPlanModels[] =
    R"({"models":[{"slug":"gpt-6.1-sol","display_name":"GPT-6.1 Sol",)"
    R"("visibility":"list"},{"slug":"codex-auto-review",)"
    R"("display_name":"Auto review","visibility":"hide"}]})";
constexpr char kPlanAnswer[] =
    "event: response.created\n"
    "data: {\"type\":\"response.created\",\"response\":{\"status\":"
    "\"in_progress\"}}\n\n"
    "event: response.completed\n"
    "data: {\"type\":\"response.completed\",\"response\":{\"status\":"
    "\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\","
    "\"content\":[{\"type\":\"output_text\",\"text\":\"Hello\"}]}]}}\n\n";

// Plays OpenAI's sign-in for apps under /auth of the test server: the
// authorize page, the token endpoint, and revocation.
class FakeOpenAiSignIn {
 public:
  // What the user does on OpenAI's page.
  enum class Answer { kAllow, kDeny, kNoPlan };

  explicit FakeOpenAiSignIn(net::test_server::EmbeddedTestServer* server) {
    server->RegisterRequestHandler(
        base::BindRepeating(&FakeOpenAiSignIn::Serve, base::Unretained(this)));
  }

  void SetAnswer(Answer answer) {
    base::AutoLock hold(lock_);
    answer_ = answer;
  }
  // The query of every authorize page that opened.
  std::vector<std::string> Authorizations() {
    base::AutoLock hold(lock_);
    return authorizations_;
  }
  // The form of every token request.
  std::vector<std::string> TokenForms() {
    base::AutoLock hold(lock_);
    return token_forms_;
  }
  std::vector<std::string> Revocations() {
    base::AutoLock hold(lock_);
    return revocations_;
  }

 private:
  static std::string Query(const GURL& url, const std::string& key) {
    std::string value;
    net::GetValueForKeyInQuery(url, key, &value);
    return value;
  }

  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request) {
    const GURL url = request.GetURL();
    const std::string issuer = url.Resolve("/auth").spec();
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    base::AutoLock hold(lock_);
    if (url.path() == "/auth/api/accounts/authorize") {
      authorizations_.emplace_back(url.query());
      nonce_ = Query(url, "nonce");
      GURL back(Query(url, "redirect_uri"));
      const std::string state = Query(url, "state");
      back =
          answer_ == Answer::kDeny
              ? net::AppendQueryParameter(
                    net::AppendQueryParameter(back, "error", "access_denied"),
                    "state", state)
              : net::AppendQueryParameter(
                    net::AppendQueryParameter(
                        net::AppendQueryParameter(back, "code", "c1"), "state",
                        state),
                    "client_id", "oaiapp_test");
      response->set_code(net::HTTP_FOUND);
      response->AddCustomHeader("Location", back.spec());
      return response;
    }
    if (url.path() == "/auth/api/accounts/oauth/token") {
      token_forms_.push_back(request.content);
      const int n = static_cast<int>(token_forms_.size());
      std::string payload =
          base::WriteJson(base::DictValue()
                              .Set("iss", issuer)
                              .Set("aud", "oaiapp_test")
                              .Set("sub", "user-1")
                              .Set("email", "ada@example.com")
                              .Set("nonce", nonce_)
                              .Set("exp", (base::Time::Now() + base::Hours(1))
                                              .InSecondsFSinceUnixEpoch()))
              .value_or("");
      std::string encoded;
      base::Base64UrlEncode(payload, base::Base64UrlEncodePolicy::OMIT_PADDING,
                            &encoded);
      response->set_content_type("application/json");
      response->set_content(
          base::WriteJson(
              base::DictValue()
                  .Set("access_token", "at-" + base::NumberToString(n))
                  .Set("refresh_token", "rt-" + base::NumberToString(n))
                  .Set("id_token", "e30." + encoded + ".sig")
                  .Set("expires_in", 3600)
                  .Set("token_type", "Bearer")
                  .Set("scope", answer_ == Answer::kNoPlan
                                    ? "openid email offline_access"
                                    : "openid email offline_access "
                                      "resource.invoke "
                                      "chatgpt.tokens.use.direct"))
              .value_or(""));
      return response;
    }
    if (url.path() == "/auth/.well-known/openid-configuration") {
      response->set_content_type("application/json");
      response->set_content(base::StrCat(
          {R"({"revocation_endpoint":")", issuer, R"(/oauth/revoke"})"}));
      return response;
    }
    if (url.path() == "/auth/oauth/revoke") {
      revocations_.push_back(request.content);
      return response;
    }
    return nullptr;
  }

  base::Lock lock_;
  Answer answer_ GUARDED_BY(lock_) = Answer::kAllow;
  std::string nonce_ GUARDED_BY(lock_);
  std::vector<std::string> authorizations_ GUARDED_BY(lock_);
  std::vector<std::string> token_forms_ GUARDED_BY(lock_);
  std::vector<std::string> revocations_ GUARDED_BY(lock_);
};

// Drives the real uw://ai page against a local server that plays the
// providers.
class UwProvidersBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    CatalogStore::SetSourceForTesting(
        embedded_test_server()->GetURL("/v1/catalog.json"));
    PlanAddresses::SetForTesting(
        {embedded_test_server()->GetURL("/auth"), Address()});
    // The test's own folder stands in for ~/.agents/skills.
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    SetAgentsSkillsFolderForTesting(AgentsSkills());
    Connectors::SetUpForTesting(home_.GetPath().AppendASCII("connectors"),
                                base::FilePath(FILE_PATH_LITERAL("/bin/sh")));
  }

  void TearDownOnMainThread() override {
    PlanAddresses::SetForTesting({GURL(chatgpt::kIssuer), chatgpt::kApi});
    SetAgentsSkillsFolderForTesting(base::FilePath());
    Connectors::SetUpForTesting(base::FilePath(), base::FilePath());
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  static base::FilePath UwSkills() {
    base::FilePath data;
    EXPECT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &data));
    return data.AppendASCII("skills");
  }

  base::FilePath AgentsSkills() {
    return home_.GetPath().AppendASCII(".agents").AppendASCII("skills");
  }

  // `more` holds more front matter lines, each ending in a newline.
  static void WriteSkillFile(const base::FilePath& folder,
                             const std::string& name,
                             const std::string& description,
                             const std::string& more = "") {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const base::FilePath skill = folder.AppendASCII(name);
    ASSERT_TRUE(base::CreateDirectory(skill));
    ASSERT_TRUE(base::WriteFile(skill.AppendASCII("SKILL.md"),
                                "---\nname: " + name +
                                    "\ndescription: " + description + "\n" +
                                    more + "---\n\nSteps.\n"));
  }

  PrefService& prefs() { return *browser()->GetProfile()->GetPrefs(); }
  content::WebContents* page() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  base::ScopedTempDir home_;

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

  // Opens uw://ai/providers, or one provider's page, once it has drawn.
  void Open(const std::string& id = "") {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://ai/providers/" + id)));
    ASSERT_EQ(true, Run("await until(() => document.querySelector('h1'));"
                        "return true;"));
  }

  // Runs `body` as the body of an async function inside the page.
  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        page(), base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  // Picks `model` in the menu of the `slot` chip, such as "Names tasks".
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

  // A Codex provider at the test server, signed in with `session`.
  std::string AddCodex(const chatgpt::Session& session) {
    const std::string id = AddPreset(ProviderPreset::kCodex, Address());
    EXPECT_TRUE(SavePlanSession(KeyAccountFor(id), session));
    return id;
  }

  // A session for ada@example.com whose access token has `left` to run.
  static chatgpt::Session Signed(base::TimeDelta left) {
    chatgpt::Session session;
    session.client_id = "oaiapp_test";
    session.email = "ada@example.com";
    session.subject = "user-1";
    session.access = "at-old";
    session.refresh = "rt-old";
    session.expires = base::Time::Now() + left;
    return session;
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer models_{embedded_test_server()};
  FakeOpenAiSignIn openai_{embedded_test_server()};
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

// Vercel AI Gateway lists its models in over half a megabyte.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, ALongModelListIsListed) {
  AddTestServer(ProviderFormat::kChatCompletions);
  base::ListValue data;
  for (int i = 0; i < 1000; ++i) {
    data.Append(base::DictValue()
                    .Set("id", "chat-" + base::NumberToString(i))
                    .Set("description", std::string(500, 'x')));
  }
  models_.Answer(
      "/v1/models",
      base::WriteJson(base::DictValue().Set("data", std::move(data))).value());
  Open();

  EXPECT_EQ(true, Run("await until(() => withText('a *', '1000 models'));"
                      "return true;"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ALanguageModelPassesItsTestWhenItAnswers) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/chat/completions", ChatAnswer("Hello"));
  Open();

  Pick("Names tasks", "chat-b");
  std::optional<ModelChoice> choice = ReadChoice(prefs(), ModelSlot::kLanguage);
  ASSERT_TRUE(choice);
  EXPECT_EQ(choice->provider_id, id);
  EXPECT_EQ(choice->model, "chat-b");
  // Picking sends nothing; only Test does.
  EXPECT_THAT(RequestsTo("/v1/chat/completions"), IsEmpty());

  EXPECT_THAT(Test("Names tasks"), HasSubstr("chat-b answered in"));
  EXPECT_THAT(RequestsTo("/v1/chat/completions"),
              ElementsAre("POST Bearer sk-test"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ALanguageModelThatSendsNoTextFailsItsTest) {
  AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/chat/completions", ChatAnswer(""));
  Open();

  Pick("Names tasks", "chat-a");
  EXPECT_THAT(Test("Names tasks"),
              HasSubstr("chat-a answered, but not with text."));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ADecisionModelPassesWhenItAnswersWithAChoice) {
  AddTestServer(ProviderFormat::kOpenAiDecisions);
  models_.Answer("/v1/models", kTwoModels);
  models_.AnswerAbout("/v1/decisions", kProbe, kProbeAnswer);
  models_.Answer("/v1/decisions", DecisionsAnswer("none", 0.9));
  Open();

  // A provider that only decides offers no language model.
  EXPECT_EQ(true, Run("const menu = await openMenu('Names tasks');"
                      "await until(() => menu.textContent.includes("
                      "    'Add a provider first'));"
                      "document.dispatchEvent(new KeyboardEvent('keydown',"
                      "    {key: 'Escape', bubbles: true}));"
                      "await until(() => !document.querySelector("
                      "    '[role=\"menu\"]'));"
                      "return true;"));
  Pick("Makes quick choices", "chat-a");
  EXPECT_THAT(Test("Makes quick choices"), HasSubstr("chat-a answered in"));
  // Asked once each whether chat-a and chat-b decide, then the test.
  EXPECT_THAT(RequestsTo("/v1/decisions"),
              ElementsAre("POST Bearer sk-test", "POST Bearer sk-test",
                          "POST Bearer sk-test"));
}

// Vercel AI Gateway marks its decision models "evaluation". They make
// decisions over its Decisions format and write no text.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       VercelsDecisionModelsAreOnlyInTheDecisionMenu) {
  AddPreset(ProviderPreset::kVercelAiGateway, Address());
  models_.Answer("/v1/models",
                 R"({"data":[{"id":"anthropic/claude-haiku-5.5",)"
                 R"("type":"language"},{"id":"typesafe-ai/jev",)"
                 R"("type":"evaluation"}]})");
  models_.Answer("/v1/decisions", DecisionsAnswer("none", 0.9));
  Open();

  EXPECT_EQ("typesafe-ai/jev",
            Run(base::StrCat(
                {"await until(() => withText('a *', '1 decision'));", kOnlyIn,
                 "return await decisionModels('typesafe-ai/jev');"})));
  EXPECT_EQ(true, Run("await openMenu('Makes quick choices');"
                      "await until(() => item('typesafe-ai/jev'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "quick-choices-menu");
  EXPECT_EQ(true, Run("document.dispatchEvent(new KeyboardEvent('keydown',"
                      "    {key: 'Escape', bubbles: true}));"
                      "await until(() => !document.querySelector("
                      "    '[role=\"menu\"]'));"
                      "return true;"));
  EXPECT_EQ(true, Run(base::StrCat(
                      {kOnlyIn,
                       "return onlyIn('Names tasks',"
                       "    'anthropic/claude-haiku-5.5',"
                       "    'typesafe-ai/jev');"})));
  Pick("Makes quick choices", "typesafe-ai/jev");
  EXPECT_THAT(Test("Makes quick choices"),
              HasSubstr("typesafe-ai/jev answered in"));
  EXPECT_THAT(RequestsTo("/v1/decisions"), ElementsAre("POST Bearer sk-test"));
}

// OpenAI's list does not say which models decide, so uw asks each once.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       OpenAisModelsAreAskedOnceWhetherTheyDecide) {
  const std::string id = AddPreset(ProviderPreset::kOpenAi, Address());
  models_.Answer("/v1/models",
                 R"({"data":[{"id":"gpt-6-luna"},{"id":"gpt-6.1-terra"}]})");
  models_.FailAbout("/v1/decisions", "gpt-6.1-terra", 400,
                    R"({"error":{"message":"gpt-6.1-terra cannot decide."}})");
  models_.Answer("/v1/decisions", kProbeAnswer);
  Open();

  EXPECT_EQ("gpt-6-luna",
            Run(base::StrCat(
                {"await until(() => withText('a *', '1 decision'));", kOnlyIn,
                 "return await decisionModels('gpt-6-luna');"})));
  // It writes text too, so it stays in the language menu.
  EXPECT_EQ(true,
            Run("const menu = await openMenu('Names tasks');"
                "await until(() => item('gpt-6.1-terra', menu));"
                "return !!item('gpt-6-luna', menu);"));
  EXPECT_EQ(RequestsTo("/v1/decisions").size(), 2u);

  Open();
  EXPECT_EQ(true, Run("await until(() => withText('a *', '1 decision'));"
                      "return true;"));
  EXPECT_EQ(RequestsTo("/v1/decisions").size(), 2u);
  SaveWindowScreenshot(browser(), "providers-decisions");

  Open(id);
  EXPECT_EQ("gpt-6-lunaCalls toolsMakes decisionsNo effort levels",
            Run("return await until(() => modelRow('gpt-6-luna'));"));
  SaveWindowScreenshot(browser(), "provider-decisions");
}

// A model that does not decide is asked again a week later, in case its
// provider has started to take it.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AModelThatSaidNoIsAskedAgainAfterAWeek) {
  const std::string id = AddPreset(ProviderPreset::kOpenAi, Address());
  const auto no = [](base::TimeDelta ago) {
    return base::DictValue().Set("decides", false).Set(
        "at", base::TimeToValue(base::Time::Now() - ago));
  };
  base::DictValue kept = base::DictValue()
                             .Set("gpt-6-luna", no(base::Days(8)))
                             .Set("gpt-6.1-terra", no(base::Days(1)));
  prefs().SetDict(kDecisionProbesPref,
                  base::DictValue().Set(
                      id, base::DictValue()
                              .Set("address", Address())
                              .Set("models", std::move(kept))));
  models_.Answer("/v1/models",
                 R"({"data":[{"id":"gpt-6-luna"},{"id":"gpt-6.1-terra"}]})");
  models_.Answer("/v1/decisions", kProbeAnswer);
  Open();

  EXPECT_EQ(true, Run("await until(() => withText('a *', '1 decision'));"
                      "return true;"));
  std::vector<std::string> asked;
  for (const ModelRequest& request : models_.Requests()) {
    if (request.path == "/v1/decisions") {
      asked.push_back(request.body);
    }
  }
  ASSERT_EQ(asked.size(), 1u);
  EXPECT_THAT(asked[0], HasSubstr(R"("model":"gpt-6-luna")"));
}

// OpenRouter lists its decision models apart from the others, and takes them
// over System One.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       OpenRoutersDecisionModelsGoOverSystemOne) {
  AddPreset(ProviderPreset::kOpenRouter, Address());
  models_.Answer("/v1/models", R"({"data":[{"id":"maker/talker"}]})");
  models_.Answer("/v1/models?output_modalities=decisions",
                 R"({"data":[{"id":"liquid/d1"}]})");
  models_.Answer("/v1/systemone", SystemOneAnswer("none", 0.9));
  Open();

  EXPECT_EQ("liquid/d1",
            Run(base::StrCat(
                {"await until(() => withText('a *', '2 models'));", kOnlyIn,
                 "return await decisionModels('liquid/d1');"})));
  EXPECT_EQ(true, Run(base::StrCat(
                      {kOnlyIn, "return onlyIn('Names tasks', "
                       "    'maker/talker', 'liquid/d1');"})));
  Pick("Makes quick choices", "liquid/d1");
  EXPECT_THAT(Test("Makes quick choices"), HasSubstr("liquid/d1 answered in"));
  EXPECT_THAT(RequestsTo("/v1/systemone"), ElementsAre("POST Bearer sk-test"));
}

// A language model can make the quick choices, marked as one. Its test asks
// it where a tab goes.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ALanguageModelMakesQuickChoices) {
  AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/chat/completions",
                 ChatAnswer(R"({\"task\":\"none\",\"confidence\":0.95,)"
                            R"(\"new_task\":0.9})"));
  Open();

  Pick("Makes quick choices", "chat-b");
  const std::optional<ModelChoice> choice =
      ReadChoice(prefs(), ModelSlot::kDecision);
  ASSERT_TRUE(choice);
  EXPECT_EQ(choice->model, "chat-b");
  EXPECT_TRUE(choice->language);
  EXPECT_EQ(true, Run("await until(() => withText("
                      "    '[aria-label=\"Makes quick choices\"] span',"
                      "    'Language model'));"
                      "return true;"));
  EXPECT_THAT(Test("Makes quick choices"), HasSubstr("chat-b answered in"));
  SaveWindowScreenshot(browser(), "quick-choices-language");
}

// With no decision model at any provider, the quick choices offer the
// language models and say where decision models come from.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       WithoutDecisionModelsQuickChoicesOfferLanguageModels) {
  AddPreset(ProviderPreset::kAnthropic, Address());
  models_.Answer("/v1/models", kTwoModels);
  Open();

  EXPECT_EQ(true, Run("const menu = await openMenu('Makes quick choices');"
                      "await until(() => item('chat-a', menu));"
                      "return menu.textContent.includes("
                      "    'OpenRouter, OpenAI, and Jev have them');"));
  SaveWindowScreenshot(browser(), "quick-choices-empty");
  EXPECT_EQ(true, Run("item('Add provider').click();"
                      "await until(dialog);"
                      "return true;"));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       RemovingAProviderTakesItsKeyAndLeavesTheChoice) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  SetChoice(prefs(), ModelSlot::kLanguage, id, "chat-a");
  models_.Answer("/v1/models", kTwoModels);
  Open(id);

  content::TestNavigationObserver back_to_list(page());
  EXPECT_EQ(true, Run("await until(() => withText('h1', 'Test server'));"
                      "await until(() => withText('span', 'Names tasks'));"
                      "withText('button', 'Remove provider').click();"
                      "return true;"));
  back_to_list.Wait();
  EXPECT_EQ(page()->GetLastCommittedURL(), GURL("chrome://ai/providers"));
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

  EXPECT_EQ(true, Run("await openMenu('Starts chats');"
                      "await until(() => item('chat-b'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "providers-menu");

  Open(id);
  EXPECT_EQ(true, Run("await until(() => modelRow('chat-b'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "provider");
  EXPECT_TRUE(console.messages().empty()) << console.GetMessageAt(0);
}

// The settings sidebar lists AI under uw, like Extensions, and opens the
// page in a new tab.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       TheSettingsSidebarOpensAiProviders) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/")));
  content::WebContentsAddedObserver opened;
  EXPECT_EQ("chrome://ai/providers",
            Run("const find = (root) => root.querySelector('#uwProvidersLink')"
                "    ?? all('*', root).map((element) => element.shadowRoot &&"
                "        find(element.shadowRoot)).find(Boolean);"
                "const link = await until(() => find(document));"
                "link.click();"
                "return link.getAttribute('href');"));
  content::WebContents* providers = opened.GetWebContents();
  EXPECT_TRUE(content::WaitForLoadStop(providers));
  EXPECT_EQ(providers->GetLastCommittedURL(), GURL("chrome://ai/providers"));
}

// Anthropic takes its key in its own header alone, for the model list and
// for the test alike, even at another address, and the test goes over
// Anthropic Messages.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AnAnthropicModelIsListedAndTestedInItsOwnFormat) {
  AddPreset(ProviderPreset::kAnthropic, Address());
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/messages", R"({"stop_reason":"end_turn","content":[)"
                                 R"({"type":"text","text":"Hello"}]})");
  Open();

  Pick("Names tasks", "chat-a");
  EXPECT_THAT(Test("Names tasks"), HasSubstr("chat-a answered in"));

  std::vector<std::string> keys;
  for (const ModelRequest& request : models_.Requests()) {
    // The catalog request carries no key.
    if (request.path == "/v1/catalog.json") {
      continue;
    }
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
            Run("const menu = await openMenu('Starts chats');"
                "const note = (name) => item(name, menu)?.dataset.note ?? '';"
                "await until(() => item('maker/talker', menu));"
                "return note('maker/talker') + '|' + note('maker/thinker');"));
}

// A Custom provider at an address the catalog lists, such as DeepSeek's,
// reads that entry's levels instead of the usual guess.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       ACustomProviderReadsTheCatalogAtItsAddress) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", R"({"data":[{"id":"deepseek-v4-pro"}]})");
  models_.Answer(
      "/v1/catalog.json",
      base::StrCat({R"({"deepseek": {"api": ")", Address(),
                    R"(", "models": {"deepseek-v4-pro": {"tool_call": true,)"
                    R"( "reasoning_options": [{"type": "toggle"}, {"type":)"
                    R"( "effort", "values": ["low", "high", "max"]}]}}}})"}));
  Open(id);
  EXPECT_EQ("deepseek-v4-proCalls toolslowhighmax",
            Run("return await until(() => modelRow('deepseek-v4-pro'));"));
}

// An effort picked for the language model goes with its requests, in the
// field its provider reads.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, AnEffortGoesWithTheModel) {
  AddPreset(ProviderPreset::kOpenRouter, Address());
  models_.Answer("/v1/models", kCatalogModels);
  models_.Answer("/v1/catalog.json", kCatalog);
  models_.Answer("/v1/chat/completions", ChatAnswer("Hello"));
  Open();
  Pick("Names tasks", "maker/thinker");
  ASSERT_EQ(
      true,
      Run("const effort = () => document.querySelector("
          "    '[aria-label=\"Names tasks\"] [aria-label=\"Effort\"]');"
          "(await until(effort)).dispatchEvent(new PointerEvent("
          "    'pointerdown', {bubbles: true, button: 0, pointerType: "
          "'mouse'}));"
          "(await until(() => all('[role=\"menu\"]')"
          "    .map((menu) => item('high', menu)).find(Boolean)))"
          "    .click();"
          "await until(() => effort().textContent.includes('high'));"
          "return true;"));
  EXPECT_EQ(ReadChoice(prefs(), ModelSlot::kLanguage)->effort, "high");

  EXPECT_THAT(Test("Names tasks"), HasSubstr("maker/thinker answered in"));
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

// A provider moves to a new address only once its models list there.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AnAddressMovesOnceItsModelsList) {
  const std::string id = AddTestServer(ProviderFormat::kChatCompletions);
  models_.Answer("/v1/models", kTwoModels);
  models_.Answer("/v1/moved/v1/models", R"({"data":[{"id":"moved-model"}]})");
  Open(id);

  const std::string nothing =
      embedded_test_server()->GetURL("/v1/nothing/v1").spec();
  EXPECT_THAT(
      Run(content::JsReplace(
              "withText('button', 'Change').click();"
              "await until(() => field('New address'));"
              "type('New address', $1);"
              "withText('button', 'Save').click();"
              "return (await until(() =>"
              "    document.querySelector('[role=alert]'))).textContent;",
              nothing))
          .ExtractString(),
      HasSubstr("(HTTP 404), so the old address stays."));
  SaveWindowScreenshot(browser(), "address-kept");
  EXPECT_EQ(FindProvider(prefs(), id)->address, Address());

  const std::string moved =
      embedded_test_server()->GetURL("/v1/moved/v1").spec();
  ASSERT_EQ(true,
            Run(content::JsReplace("type('New address', $1);"
                                   "withText('button', 'Save').click();"
                                   "await until(() => modelRow('moved-model'));"
                                   "return true;",
                                   moved)));
  EXPECT_EQ(FindProvider(prefs(), id)->address, moved);
  EXPECT_EQ(ApiKeyStore().GetKey(KeyAccountFor(id)), "sk-test");
}

// At a proxy that leaves the format to each model, the catalog names each
// model's maker: Claude goes over Anthropic Messages with the key both ways,
// GPT over OpenAI Responses, and the rest over chat completions.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       EachModelGoesOverItsMakersFormat) {
  const std::string id = AddTestServer(ProviderFormat::kEachModel);
  models_.Answer("/v1/catalog.json", R"({
    "anthropic": {"models": {"claude-x": {"tool_call": true,
        "reasoning_options": [{"type": "effort", "values": ["low", "high"]}]}}},
    "openai": {"models": {"gpt-x": {"tool_call": true}}}})");
  models_.Answer(
      "/v1/models",
      R"({"data":[{"id":"claude-x"},{"id":"gpt-x"},{"id":"other-x"}]})");
  models_.Answer("/v1/messages", R"({"stop_reason":"end_turn","content":[)"
                                 R"({"type":"text","text":"Hello"}]})");
  models_.Answer("/v1/responses",
                 R"({"status":"completed","output":[{"type":"message",)"
                 R"("role":"assistant","content":[{"type":"output_text",)"
                 R"("text":"Hello"}]}]})");
  Open(id);
  EXPECT_EQ("claude-xAnthropic MessagesCalls toolslowhigh",
            Run("return await until(() => modelRow('claude-x'));"));
  EXPECT_EQ("gpt-xOpenAI ResponsesCalls toolsNo effort levels",
            Run("return await until(() => modelRow('gpt-x'));"));
  EXPECT_EQ("other-xChat completionsCalls toolslowmediumhigh",
            Run("return await until(() => modelRow('other-x'));"));
  SaveWindowScreenshot(browser(), "each-model");

  Open();
  Pick("Names tasks", "claude-x");
  EXPECT_EQ(ReadChoice(prefs(), ModelSlot::kLanguage)->format,
            ProviderFormat::kAnthropicMessages);
  EXPECT_THAT(Test("Names tasks"), HasSubstr("claude-x answered in"));
  Pick("Names tasks", "gpt-x");
  EXPECT_THAT(Test("Names tasks"), HasSubstr("gpt-x answered in"));

  std::vector<std::string> sent;
  for (const ModelRequest& request : models_.Requests()) {
    if (request.method == "POST") {
      sent.push_back(request.path + " " + request.authorization + " / " +
                     request.anthropic_key);
    }
  }
  EXPECT_THAT(sent, ElementsAre("/v1/messages Bearer sk-test / sk-test",
                                "/v1/responses Bearer sk-test / "));
}

// OpenCode Go serves every model at one address, each in the format its
// entry in the catalog names. It refuses a request without a session id, and
// each Test is a conversation of its own. Every request says it is uw's.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       OpenCodeGoSendsEachModelInItsOwnFormat) {
  models_.Answer("/v1/catalog.json", R"({
    "opencode-go": {"api": "https://opencode.ai/inference/go/openai/v1",
                    "models": {
      "kimi-k3": {"tool_call": true},
      "minimax-m3": {"tool_call": true,
                     "provider": {"npm": "@ai-sdk/anthropic"}},
      "gpt-6-luna": {"tool_call": true, "provider": {"npm": "@ai-sdk/openai"}}
    }}})");
  models_.Answer(
      "/v1/models",
      R"({"data":[{"id":"kimi-k3"},{"id":"minimax-m3"},{"id":"gpt-6-luna"}]})");
  models_.Answer("/v1/chat/completions", ChatAnswer("Hello"));
  models_.Answer("/v1/messages", R"({"stop_reason":"end_turn","content":[)"
                                 R"({"type":"text","text":"Hello"}]})");
  models_.Answer("/v1/responses",
                 R"({"status":"completed","output":[{"type":"message",)"
                 R"("role":"assistant","content":[{"type":"output_text",)"
                 R"("text":"Hello"}]}]})");
  Open();
  EXPECT_EQ("https://opencode.ai/inference/go/v1",
            Run("withText('button', 'Add provider').click();"
                "await until(dialog);"
                "withText('button', 'OpenCode Go', dialog()).click();"
                "return (await until(() => field('Address'))).value;"));
  SaveWindowScreenshot(browser(), "add-opencode-go");
  ASSERT_EQ(true, Run(content::JsReplace(
                      "type('Address', $1);"
                      "type('Key', 'sk-test');"
                      "withText('button', 'Add provider', dialog()).click();"
                      "await until(() => !dialog());"
                      "return true;",
                      Address())));
  const std::optional<Provider> go = FindProvider(prefs(), "opencode-go");
  ASSERT_TRUE(go);
  EXPECT_EQ(go->format, ProviderFormat::kEachModel);

  Open(go->id);
  EXPECT_THAT(
      Run("return await until(() => modelRow('kimi-k3'));").ExtractString(),
      HasSubstr("Chat completions"));
  EXPECT_THAT(
      Run("return await until(() => modelRow('minimax-m3'));").ExtractString(),
      HasSubstr("Anthropic Messages"));
  EXPECT_THAT(
      Run("return await until(() => modelRow('gpt-6-luna'));").ExtractString(),
      HasSubstr("OpenAI Responses"));
  EXPECT_EQ(true, Run("return document.body.textContent.includes("
                      "'entry for this provider names each model');"));
  SaveWindowScreenshot(browser(), "opencode-go");

  Open();
  for (const char* model : {"kimi-k3", "minimax-m3", "gpt-6-luna"}) {
    Pick("Names tasks", model);
    EXPECT_THAT(Test("Names tasks"), HasSubstr(" answered in"));
  }

  std::vector<std::string> sent;
  std::set<std::string> sessions;
  for (const ModelRequest& request : models_.Requests()) {
    if (request.path == "/v1/catalog.json") {
      continue;
    }
    EXPECT_THAT(request.user_agent, StartsWith("uw/")) << request.path;
    if (request.method == "POST") {
      sent.push_back(request.path + " " + request.authorization + " / " +
                     request.anthropic_key);
      EXPECT_FALSE(request.session.empty()) << request.path;
      sessions.insert(request.session);
    }
  }
  EXPECT_THAT(sent, ElementsAre("/v1/chat/completions Bearer sk-test / ",
                                "/v1/messages Bearer sk-test / sk-test",
                                "/v1/responses Bearer sk-test / "));
  EXPECT_EQ(sessions.size(), 3u);
}

// A preset fills in its own address, which the user can change, say to a
// company gateway. It stays that preset, with its own format.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       APresetCanStartAtAnotherAddress) {
  models_.Answer("/v1/models", kTwoModels);
  Open();
  EXPECT_EQ("https://api.openai.com/v1",
            Run("withText('button', 'Add provider').click();"
                "await until(dialog);"
                "withText('button', 'OpenAI', dialog()).click();"
                "return (await until(() => field('Address'))).value;"));
  ASSERT_EQ(true, Run(content::JsReplace(
                      "type('Address', $1);"
                      "type('Key', 'sk-test');"
                      "withText('button', 'Add provider', dialog()).click();"
                      "await until(() => !dialog());"
                      "return true;",
                      Address())));
  const std::optional<Provider> openai = FindProvider(prefs(), "openai");
  ASSERT_TRUE(openai);
  EXPECT_EQ(openai->address, Address());
  EXPECT_EQ(openai->format, ProviderFormat::kOpenAiResponses);
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

// uw://ai opens on its providers, and each section has its own address.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, UwAiOpensOnItsProviders) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/")));
  EXPECT_EQ("Providers – AI",
            Run("await until(() => withText('h1', 'Providers'));"
                "return document.title;"));
  EXPECT_EQ("page", Run("return document.getElementById('section-providers')"
                        "    .getAttribute('aria-current');"));
  EXPECT_EQ("/skills", Run("return document.getElementById('section-skills')"
                           "    .getAttribute('href');"));
}

// "Back to chat" opens a new chat when the window has none, and otherwise goes
// to the chat tab used last, even one opened before another.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       BackToChatGoesToTheChatUsedLast) {
  TabStripModel* tabs = browser()->tab_strip_model();
  Open();
  SaveWindowScreenshot(browser(), "back-to-chat");
  content::WebContents* ai = page();
  const int count = tabs->count();
  const auto back = [&] {
    tabs->ActivateTabAt(tabs->GetIndexOfWebContents(ai));
    ASSERT_TRUE(content::ExecJs(
        ai, "document.getElementById('back-to-chat').click();"));
  };
  const auto is_chat = [](content::WebContents* contents) {
    return contents->GetVisibleURL().host() == "chat";
  };

  back();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tabs->count() == count + 1 && is_chat(page()); }));
  content::WebContents* first = page();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://chat/"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* second = page();
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(first));

  back();
  EXPECT_TRUE(base::test::RunUntil([&] { return page() == first; }));
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(second));
  back();
  EXPECT_TRUE(base::test::RunUntil([&] { return page() == second; }));
  EXPECT_EQ(tabs->count(), count + 2);
}

// uw://ai/skills lists the skills of both folders, and a switch turns one off
// for every chat.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, TheSkillsSectionTurnsASkillOff) {
  WriteSkillFile(UwSkills(), "book-activities", "Books tours and tickets.");
  WriteSkillFile(AgentsSkills(), "research", "Researches a question.");
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/skills")));
  EXPECT_EQ("2 on", Run("return await until(() => {"
                        "  const count = document.querySelector("
                        "      '#section-skills span:last-child').textContent;"
                        "  return count === '2 on' && count;"
                        "});"));
  EXPECT_EQ("1 on",
            Run("document.querySelector('[data-skill=\"research\"]').click();"
                "return await until(() => {"
                "  const count = document.querySelector("
                "      '#section-skills span:last-child').textContent;"
                "  return count === '1 on' && count;"
                "});"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return prefs().GetList(kAgentDisabledSkillsPref).contains("research");
  }));
  SaveWindowScreenshot(browser(), "skills");
}

// A skill only one side may start says which.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, TheSkillsSectionSaysWhoStarts) {
  WriteSkillFile(AgentsSkills(), "handoff", "Hands the work off.",
                 "disable-model-invocation: true\n");
  WriteSkillFile(UwSkills(), "site-manners", "Acts politely on sites.",
                 "user-invocable: false\n");
  WriteSkillFile(AgentsSkills(), "research", "Researches a question.");
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/skills")));
  // uw's own skills come first.
  EXPECT_EQ("site-manners: Only the agent | handoff: Only you | research: ",
            Run("await until(() => all('[data-skill-row]').length === 3);"
                "return all('[data-skill-row]').map((row) =>"
                "    row.dataset.skillRow + ': ' +"
                "    (row.querySelector('[data-who]')?.textContent ?? ''))"
                "    .join(' | ');"));
}

// The test connector program.
std::string FakeServer() {
  base::FilePath program;
  EXPECT_TRUE(base::PathService::Get(base::DIR_EXE, &program));
  return program.AppendASCII("uw_fake_mcp_server").AsUTF8Unsafe();
}

// A registry with a server that asks for a sign-in and one that needs a key,
// with the sign-in server's pages. Served over https, as registry servers are.
std::unique_ptr<net::test_server::HttpResponse> ServeCatalog(
    const net::test_server::HttpRequest& request) {
  const std::string base = request.base_url.spec();
  const std::string issuer = base.substr(0, base.size() - 1);
  const std::string path(request.GetURL().path());
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("application/json");
  if (path == "/v0/servers") {
    response->set_content(base::StrCat(
        {R"({"servers": [
          {"server": {"name": "io.github.a/signs-in", "title": "Signs in",
                      "remotes": [{"type": "streamable-http", "url": ")",
         base, R"(needs-sign-in"}]}},
          {"server": {"name": "io.github.a/keyed", "title": "Keyed",
                      "remotes": [{"type": "streamable-http", "url": ")",
         base, R"(keyed", "headers": [{"name": "Authorization",
                      "description": "Your key", "isRequired": true,
                      "isSecret": true}]}]}}]})"}));
  } else if (path == "/needs-sign-in") {
    response->set_code(net::HTTP_UNAUTHORIZED);
    response->AddCustomHeader(
        "WWW-Authenticate",
        base::StrCat({"Bearer resource_metadata=\"", base,
                      ".well-known/oauth-protected-resource/needs-sign-in\""}));
  } else if (path == "/.well-known/oauth-protected-resource/needs-sign-in") {
    response->set_content(base::StrCat({R"({"resource": ")", base,
                                        R"(needs-sign-in", "authorization_servers": [")",
                                        issuer, R"("]})"}));
  } else if (path == "/.well-known/oauth-authorization-server") {
    response->set_content(base::StrCat(
        {R"({"issuer": ")", issuer, R"(", "authorization_endpoint": ")", base,
         R"(authorize", "token_endpoint": ")", base,
         R"(token", "registration_endpoint": ")", base,
         R"(register", "code_challenge_methods_supported": ["S256"]})"}));
  } else if (path == "/register") {
    response->set_content(R"({"client_id": "client-1"})");
  } else if (path == "/authorize") {
    response->set_content_type("text/html");
    response->set_content("Sign in here");
  } else {
    return nullptr;
  }
  return response;
}

// The connect dialog opens from uw://ai/connectors?add with Recommended. A
// server found in the registry opens its sign-in as soon as it is added, and
// one that needs a key asks for it.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       TheConnectDialogAddsFromTheCatalog) {
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  https.RegisterRequestHandler(base::BindRepeating(&ServeCatalog));
  ASSERT_TRUE(https.Start());
  ConnectorCatalogStore::SetUpForTesting(
      home_.GetPath().AppendASCII("connectors"), https.GetURL("/"),
      https.GetURL("/raw/"), g_browser_process->shared_url_loader_factory());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://ai/connectors?add")));
  EXPECT_EQ(8, Run("await until(() =>"
                   "    all('[data-entry^=\"recommended:\"]').length >= 8);"
                   "return all('[data-entry^=\"recommended:\"]').length;"));
  SaveWindowScreenshot(browser(), "connect-dialog");
  // Types `query` and clicks Add on the entry `id` once it shows.
  const auto search = [&](std::string_view query, std::string_view id) {
    return Run(content::JsReplace(
        "const box = document.getElementById('catalog-search');"
        "Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')"
        "    .set.call(box, $1);"
        "box.dispatchEvent(new Event('input', {bubbles: true}));"
        "const entry = await until(() => document.querySelector($2));"
        "const said = entry.textContent;"
        "entry.querySelector('[data-action=\"Add\"]').click();"
        "return said;",
        query, base::StrCat({"[data-entry=\"", id, "\"]"})));
  };

  content::WebContents* const settings = page();
  EXPECT_THAT(search("signs", "registry:io.github.a/signs-in").ExtractString(),
              HasSubstr("MCP Registry"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return page() != settings && page()->GetVisibleURL().path() == "/authorize";
  }));
  ASSERT_TRUE(Connectors::Get().Find("signs-in"));

  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(settings));
  EXPECT_THAT(search("keyed", "registry:io.github.a/keyed").ExtractString(),
              HasSubstr("asks for a key"));
  ASSERT_EQ(true,
            Run("const key = await until(() =>"
                "    document.querySelector('[data-field=\"Authorization\"]'));"
                "Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"
                "    'value').set.call(key, 'Bearer k1');"
                "key.dispatchEvent(new Event('input', {bubbles: true}));"
                "document.querySelector("
                "    '[data-action=\"Add with fields\"]').click();"
                "return true;"));
  EXPECT_TRUE(
      base::test::RunUntil([] { return Connectors::Get().Find("keyed"); }));
  EXPECT_THAT(ApiKeyStore().GetKey("connector:keyed").value_or(""),
              HasSubstr("Bearer k1"));
  SaveWindowScreenshot(browser(), "connect-dialog-added");
  ConnectorCatalogStore::SetUpForTesting(base::FilePath(), GURL(), GURL(),
                                         nullptr);
}

// uw://ai/connectors adds a connector from pasted JSON and starts it, and its
// page turns one of its tools off.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, AConnectorIsAddedByPaste) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/connectors")));
  const std::string json =
      base::StrCat({R"({"mcpServers": {"fake": {"command": ")", FakeServer(),
                    R"(", "env": {"FAKE_KEY": "s3cret"}}}})"});
  // The Mac keeps it in the Keychain, and Windows in a file encrypted for the
  // Windows user.
#if BUILDFLAG(IS_MAC)
  constexpr char kKept[] = "FAKE_KEY goes to Keychain";
#else
  constexpr char kKept[] = "FAKE_KEY is saved encrypted";
#endif
  EXPECT_THAT(
      Run(content::JsReplace(
              "(await until(() => document.getElementById('add-connector')))"
              "    .click();"
              "(await until(() => document.getElementById('paste-connector')))"
              "    .click();"
              "const box = await until(() =>"
              "    document.getElementById('connector-json'));"
              "Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,"
              "    'value').set.call(box, $1);"
              "box.dispatchEvent(new Event('input', {bubbles: true}));"
              "const found = await until(() =>"
              "    document.querySelector('[data-found=\"fake\"]'));"
              "const said = found.textContent;"
              "document.getElementById('add-connectors').click();"
              "const row = await until(() => {"
              "  const row = document.querySelector('[data-row=\"fake\"]');"
              "  return row && row.textContent.includes('Running') && row;"
              "});"
              "return said + ' | ' + row.textContent;",
              json))
          .ExtractString(),
      ::testing::AllOf(::testing::HasSubstr(kKept),
                       ::testing::HasSubstr("5 tools"),
                       ::testing::HasSubstr("Running")));
  SaveWindowScreenshot(browser(), "connectors");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://ai/connectors/fake")));
  ASSERT_EQ(true, Run("(await until(() => document.querySelector("
                      "    '[data-tool-switch=\"crash\"]'))).click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([] {
    return Connectors::Get().Find("fake")->tools_off.contains("crash");
  }));
  EXPECT_THAT(Run("return document.querySelector('[data-tool=\"echo\"]')"
                  "    .textContent;")
                  .ExtractString(),
              ::testing::HasSubstr("Reads only"));
  SaveWindowScreenshot(browser(), "connector");
}

// A connector that fails shows its last error and the last lines of its log.
// Open in terminal runs its command in a terminal tab, and Show log opens the
// log.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest,
                       AFailedConnectorOpensInTerminal) {
  Connectors::Get().Start();
  ASSERT_TRUE(base::test::RunUntil([] { return Connectors::Get().loaded(); }));
  ConnectorConfig failing;
  failing.name = "railway";
  failing.command = FakeServer();
  failing.args = {"--fail=Unauthorized. Please login with railway login"};
  std::vector<ConnectorConfig> added;
  added.push_back(std::move(failing));
  ASSERT_TRUE(Connectors::Get().Add(std::move(added)).has_value());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/connectors")));
  EXPECT_EQ("Unauthorized. Please login with railway login",
            Run("await until(() => document.querySelector("
                "    '[data-log=\"railway\"] pre'));"
                "return document.querySelector("
                "    '[data-row=\"railway\"] [data-line]').textContent;"));
  EXPECT_THAT(
      Run("return document.querySelector("
          "    '[data-log=\"railway\"] pre').textContent;")
          .ExtractString(),
      HasSubstr("starting\nUnauthorized. Please login with railway login"));
  content::WebContents* const settings = page();
  ASSERT_EQ(true, Run("document.querySelector('[data-log=\"railway\"] "
                      "[data-action=\"Show log\"]').click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return page() != settings &&
           page()->GetVisibleURL().spec().ends_with("/connectors/railway.log");
  }));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(settings));
  ASSERT_EQ(true, Run("document.querySelector('[data-log=\"railway\"] "
                      "[data-action=\"Open in terminal\"]').click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return IsTerminalUrl(page()->GetVisibleURL()); }));
}

IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, TheSkillsFolderOpens) {
  // Finder stays shut.
  platform_util::internal::DisableShellOperationsForTesting();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/skills")));
  ASSERT_EQ(true, Run("(await until(() =>"
                      "    document.getElementById('open-skills-folder')))"
                      "    .click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return base::DirectoryExists(UwSkills());
  }));
}

// Keeps `text` in the profile's memory, from the chat `chat`.
void KeepMemory(Profile* profile, std::string text, std::string chat = {}) {
  bool done = false;
  MemoryStore::For(profile)->Keep(
      std::move(text), false, std::move(chat), 0,
      base::BindLambdaForTesting(
          [&](base::expected<MemoryStore::KeepResult, std::string> kept) {
            EXPECT_TRUE(kept.has_value());
            done = true;
          }));
  ASSERT_TRUE(base::test::RunUntil([&] { return done; }));
}

// Every memory of `profile`, newest first.
std::vector<Memory> MemoriesOf(Profile* profile) {
  std::optional<std::vector<Memory>> found;
  MemoryStore::For(profile)->Search(
      {}, 100, base::BindLambdaForTesting([&](std::vector<Memory> all) {
        found = std::move(all);
      }));
  EXPECT_TRUE(base::test::RunUntil([&] { return found.has_value(); }));
  return found.value_or(std::vector<Memory>());
}

// uw://ai/memory lists the memories with the chat each came from, searches
// them, rewords one, forgets one, and forgets them all.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, TheMemoryPageChangesMemories) {
  Profile* profile = browser()->GetProfile();
  ChatIndex* index = ChatIndex::For(profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return index->loaded(); }));
  ChatIndex::Entry bill;
  bill.id = ChatFile::NewChatId();
  bill.title = "Pay the PG&E bill";
  bill.updated = base::Time::Now();
  index->Update(bill);
  KeepMemory(profile, "Banks with Chase.", bill.id);
  KeepMemory(profile, "Home airport is SFO.", ChatFile::NewChatId());
  KeepMemory(profile, "Prefers aisle seats on flights.");
  const std::vector<Memory> kept = MemoriesOf(profile);
  ASSERT_EQ(kept.size(), 3u);
  const std::string aisle = base::NumberToString(kept[0].id);
  const std::string chase = base::NumberToString(kept[2].id);

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/memory")));
  EXPECT_EQ("3 memories",
            Run("return await until(() => document.getElementById("
                "    'memory-count')?.textContent);"));
  EXPECT_EQ("3", Run("return await until(() => document.querySelector("
                     "    '#section-memory span:last-child').textContent);"));
  EXPECT_EQ(
      "Prefers aisle seats on flights.|Home airport is SFO.|Banks with Chase.",
      Run("return all('.memory-text').map((row) => row.textContent)"
          "    .join('|');"));
  EXPECT_THAT(
      Run("return all('.memory-row').map((row) => row.textContent).join('|');")
          .ExtractString(),
      testing::AllOf(testing::HasSubstr("From Pay the PG&E bill"),
                     testing::HasSubstr("From a deleted chat")));
  SaveWindowScreenshot(browser(), "memory-page");

  EXPECT_EQ("1 of 3", Run(R"(
      const box = document.getElementById('memory-search');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
          .set.call(box, 'aisle');
      box.dispatchEvent(new Event('input', {bubbles: true}));
      return await until(() => all('.memory-row').length === 1 &&
          [...document.querySelectorAll('span')].find((span) =>
              span.textContent === '1 of 3')?.textContent);)"));
  EXPECT_EQ("aisle", Run("return document.querySelector('.memory-text mark')"
                         "    .textContent;"));
  SaveWindowScreenshot(browser(), "memory-search");

  EXPECT_EQ("Prefers window seats on long flights.",
            Run(content::JsReplace(R"(
      const search = document.getElementById('memory-search');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
          .set.call(search, '');
      search.dispatchEvent(new Event('input', {bubbles: true}));
      await until(() => all('.memory-row').length === 3);
      document.querySelector('[data-reword="' + $1 + '"]').click();
      const draft = await until(() => document.getElementById('memory-draft'));
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
          .set.call(draft, 'Prefers window seats on long flights.');
      draft.dispatchEvent(new Event('input', {bubbles: true}));
      document.getElementById('save-memory').click();
      return await until(() => !document.getElementById('memory-draft') &&
          document.querySelector('.memory-text')?.textContent ===
              'Prefers window seats on long flights.' &&
          document.querySelector('.memory-text').textContent);)",
                                   aisle)));
  EXPECT_EQ(MemoriesOf(profile)[0].text,
            "Prefers window seats on long flights.");

  EXPECT_EQ("2 memories", Run(content::JsReplace(R"(
      (await until(() => document.querySelector(
          '[data-forget="' + $1 + '"]'))).click();
      return await until(() => {
        const count = document.getElementById('memory-count').textContent;
        return count === '2 memories' && count;
      });)",
                                                 chase)));

  EXPECT_EQ(true, Run(R"(
      document.getElementById('forget-all-memories').click();
      (await until(() => document.getElementById('confirm-forget-all')))
          .click();
      return Boolean(await until(() =>
          document.getElementById('no-memories')));)"));
  EXPECT_TRUE(MemoriesOf(profile).empty());
}

// The page pauses memory for the profile, and shows a memory a chat keeps
// while it is open.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, TheMemoryPagePausesAndListens) {
  Profile* profile = browser()->GetProfile();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/memory")));
  ASSERT_EQ(true, Run("return Boolean(await until(() =>"
                      "    document.getElementById('no-memories')));"));

  EXPECT_THAT(Run("document.getElementById('use-memory').click();"
                  "return await until(() => {"
                  "  const note = document.getElementById('memory-use-note')"
                  "      .textContent;"
                  "  return note.startsWith('Paused') && note;"
                  "});")
                  .ExtractString(),
              testing::HasSubstr("Nothing is recalled or kept"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !profile->GetPrefs()->GetBoolean(kAgentMemoryPref); }));

  KeepMemory(profile, "Goes by Sam.");
  EXPECT_EQ("Goes by Sam.", Run("return await until(() =>"
                                "    document.querySelector('.memory-text')"
                                "        ?.textContent);"));
  SaveWindowScreenshot(browser(), "memory-paused");
}

// Continue with ChatGPT signs in on OpenAI's page in a tab, registers uw
// for the account, and adds Codex with the plan's models under their
// ChatGPT names. Its requests are streamed, with the plan's rules.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, ContinueWithChatGptAddsCodex) {
  models_.Answer("/v1/models", kPlanModels);
  models_.Answer("/v1/responses", kPlanAnswer);
  Open();
  const int tabs = browser()->tab_strip_model()->count();
  ASSERT_EQ(true, Run("withText('button', 'Add provider').click();"
                      "await until(dialog);"
                      "withText('button', 'Codex', dialog()).click();"
                      "await until(() => dialog().textContent.includes("
                      "    'Your ChatGPT Plus or Pro plan'));"
                      "return true;"));
  SaveWindowScreenshot(browser(), "add-codex");
  ASSERT_EQ(true,
            Run("withText('button', 'Continue with ChatGPT', dialog()).click();"
                "await until(() => !dialog());"
                "return true;"));
  // The sign-in tab closed itself.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return browser()->tab_strip_model()->count() == tabs; }));
  EXPECT_THAT(openai_.Authorizations(),
              ElementsAre(AllOf(HasSubstr("client_id=dynamic_agent_client"),
                                HasSubstr("agent_name_hint=uw"),
                                HasSubstr("chatgpt.tokens.use.direct"))));
  const std::optional<Provider> codex = FindProvider(prefs(), "codex");
  ASSERT_TRUE(codex);
  EXPECT_EQ(codex->preset, ProviderPreset::kCodex);
  const std::optional<chatgpt::Session> session =
      ReadPlanSession(codex->key_account);
  ASSERT_TRUE(session);
  EXPECT_EQ(session->client_id, "oaiapp_test");
  EXPECT_EQ(session->email, "ada@example.com");
  EXPECT_EQ(session->access, "at-1");
  EXPECT_THAT(Run("return await until(() => all('a').find((row) =>"
                  "    row.textContent.includes('ada@example.com'))"
                  "    ?.textContent);")
                  .ExtractString(),
              HasSubstr("Signed in"));

  Open(codex->id);
  EXPECT_THAT(
      Run("return await until(() => modelRow('gpt-6.1-sol'));").ExtractString(),
      HasSubstr("GPT-6.1 Sol"));
  EXPECT_EQ(false, Run("return !!modelRow('codex-auto-review');"));
  SaveWindowScreenshot(browser(), "codex");

  Open();
  ASSERT_EQ(true, Run("const menu = await openMenu('Names tasks');"
                      "(await until(() => item('GPT-6.1 Sol', menu))).click();"
                      "await until(() =>"
                      "    chip('Names tasks').textContent.includes("
                      "        'GPT-6.1 Sol'));"
                      "return true;"));
  EXPECT_THAT(Test("Names tasks"), HasSubstr("answered in"));
  std::vector<std::string> sent;
  for (const ModelRequest& request : models_.Requests()) {
    if (request.path != "/v1/responses") {
      continue;
    }
    EXPECT_EQ(request.authorization, "Bearer at-1");
    const std::optional<base::DictValue> body =
        base::JSONReader::ReadDict(request.body, base::JSON_PARSE_RFC);
    ASSERT_TRUE(body);
    EXPECT_EQ(body->FindBool("stream"), true);
    EXPECT_EQ(body->FindBool("store"), false);
    EXPECT_FALSE(body->Find("max_output_tokens"));
    sent.push_back(*body->FindString("model"));
  }
  EXPECT_THAT(sent, ElementsAre("gpt-6.1-sol"));
}

// A token about to run out is refreshed before the request goes, and the
// new refresh token replaces the old.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, CodexRefreshesItsTokenFirst) {
  models_.Answer("/v1/models", kPlanModels);
  models_.Answer("/v1/responses", kPlanAnswer);
  const std::string id = AddCodex(Signed(base::Seconds(20)));
  SetChoice(prefs(), ModelSlot::kLanguage, id, "gpt-6.1-sol");
  Open();
  EXPECT_THAT(Test("Names tasks"), HasSubstr("answered in"));
  EXPECT_THAT(openai_.TokenForms(),
              ElementsAre(AllOf(HasSubstr("grant_type=refresh_token"),
                                HasSubstr("refresh_token=rt-old"),
                                HasSubstr("client_id=oaiapp_test"))));
  EXPECT_EQ(ReadPlanSession(KeyAccountFor(id))->refresh, "rt-1");
  for (const ModelRequest& request : models_.Requests()) {
    if (request.path == "/v1/responses") {
      EXPECT_EQ(request.authorization, "Bearer at-1");
    }
  }
}

// Sign out drops the tokens at once, keeps the account for the next
// sign-in, and asks OpenAI to revoke the refresh token.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, SigningOutOfCodexRevokesIt) {
  models_.Answer("/v1/models", kPlanModels);
  const std::string id = AddCodex(Signed(base::Hours(1)));
  Open(id);
  ASSERT_EQ(true, Run("(await until(() => withText('button', 'Sign out')))"
                      "    .click();"
                      "await until(() =>"
                      "    withText('button', 'Continue with ChatGPT'));"
                      "return true;"));
  const std::optional<chatgpt::Session> session =
      ReadPlanSession(KeyAccountFor(id));
  ASSERT_TRUE(session);
  EXPECT_FALSE(session->HasTokens());
  EXPECT_EQ(session->email, "ada@example.com");
  EXPECT_EQ(session->client_id, "oaiapp_test");
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !openai_.Revocations().empty(); }));
  EXPECT_THAT(openai_.Revocations(),
              ElementsAre(AllOf(HasSubstr("token=rt-old"),
                                HasSubstr("token_type_hint=refresh_token"))));
  SaveWindowScreenshot(browser(), "codex-signed-out");
}

// Saying no on OpenAI's page, or an account without a plan, adds nothing
// and says why.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, ASignInCanEndWithoutCodex) {
  // Returns the dialog's error once it holds `expected`.
  const auto sign_in = [&](const std::string& expected) {
    return Run(content::JsReplace(
                   "if (!dialog()) {"
                   "  withText('button', 'Add provider').click();"
                   "  await until(dialog);"
                   "  withText('button', 'Codex', dialog()).click();"
                   "}"
                   "(await until(() =>"
                   "    withText('button', 'Continue with ChatGPT', dialog())))"
                   "    .click();"
                   "return await until(() => {"
                   "  const text = dialog().querySelector('[role=\"alert\"]')"
                   "      ?.textContent ?? '';"
                   "  return text.includes($1) && text;"
                   "});",
                   expected))
        .ExtractString();
  };
  Open();
  openai_.SetAnswer(FakeOpenAiSignIn::Answer::kDeny);
  EXPECT_EQ(sign_in("didn't let uw"),
            "You didn't let uw use your ChatGPT plan, so Codex wasn't added.");
  openai_.SetAnswer(FakeOpenAiSignIn::Answer::kNoPlan);
  EXPECT_EQ(sign_in("no plan"),
            "This ChatGPT account has no plan uw can use. Codex needs ChatGPT "
            "Plus or Pro.");
  EXPECT_FALSE(FindProvider(prefs(), "codex"));
}

// A usage limit ends the stream with OpenAI's words, which the test line
// shows with where to see the plan's usage.
IN_PROC_BROWSER_TEST_F(UwProvidersBrowserTest, CodexShowsAUsageLimit) {
  models_.Answer("/v1/models", kPlanModels);
  models_.Answer(
      "/v1/responses",
      "data: {\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":"
      "\"subscription_sharing_usage_limit_exceeded\",\"message\":\"You've "
      "reached this week's limit for apps.\"}}}\n\n");
  const std::string id = AddCodex(Signed(base::Hours(1)));
  SetChoice(prefs(), ModelSlot::kLanguage, id, "gpt-6.1-sol");
  Open();
  EXPECT_THAT(Test("Names tasks"),
              AllOf(HasSubstr("You've reached this week's limit for apps."),
                    HasSubstr("chatgpt.com/settings/usage")));
}

}  // namespace
}  // namespace uw
