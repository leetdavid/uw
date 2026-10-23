// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "services/network/public/cpp/data_element.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/switches.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/agent/skill_folders.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

struct Site {
  std::string_view host;
  std::string_view name;
  std::string_view letter;
  std::string_view color;
};

constexpr Site kSites[] = {
    {"travel.example", "Wayfarer", "W", "#0E7490"},
    {"tram.example", "Lisboa Transit", "L", "#CA8A04"},
    {"flights.example", "Skyline", "S", "#2563EB"},
    {"stays.example", "Nest", "N", "#E11D48"},
    {"eat.example", "Bite", "B", "#EA580C"},
    {"search.example", "Search", "Q", "#6B7280"},
    {"rust.example", "Rust notes", "R", "#B45309"},
    {"tokio.example", "Tokio", "T", "#7C3AED"},
    {"bikes.example", "Cadence", "C", "#16A34A"},
    {"list.example", "Lists", "L", "#0891B2"},
    {"mail.example", "Mail", "M", "#2563EB"},
    {"calendar.example", "Calendar", "31", "#DC2626"},
    {"chat.example", "Chat", "C", "#7C3AED"},
    {"docs.example", "Docs", "D", "#0284C7"},
    {"ci.example", "CI", "CI", "#111827"},
};

struct Page {
  std::string_view url;
  std::string_view title;
  std::string_view text;
};

constexpr Page kLisbon = {"https://travel.example/lisbon-in-four-days",
                          "Four days in Lisbon",
                          "Hills, trams, and tiled houses."};
constexpr Page kTram = {"https://tram.example/28",
                        "Tram 28: the route and the best stops",
                        "Get on at Martim Moniz."};
constexpr Page kFlights = {"https://flights.example/lis",
                           "Flights to Lisbon, May 14 to 18",
                           "Twelve direct flights."};
constexpr Page kStays = {"https://stays.example/alfama", "Apartments in Alfama",
                         "Forty-two places to stay."};
constexpr Page kPastry = {
    "https://eat.example/lisbon/pastel-de-nata",
    "The best pastel de nata in Lisbon",
    "Crisp, blistered, and still warm from the oven. We ate our way through "
    "the city's custard tarts so you don't have to. Start in Belem, where the "
    "recipe has been a secret since 1837, then work back towards the river."};
constexpr Page kSearch = {"https://search.example/?q=pastel+de+nata+lisbon",
                          "pastel de nata lisbon", "Results."};
constexpr Page kAsync = {"https://rust.example/async",
                         "Asynchronous programming in Rust",
                         "Futures, executors, and wakers."};
constexpr Page kPin = {"https://rust.example/async/pinning",
                       "Pin and Unpin, explained",
                       "Why futures must not move."};
constexpr Page kSelect = {"https://tokio.example/select", "The select! macro",
                          "Wait on several branches at once."};
constexpr Page kSizing = {"https://bikes.example/sizing",
                          "Road bike sizing guide", "Measure your inseam."};
constexpr Page kGroupsets = {"https://bikes.example/105-vs-tiagra",
                             "Shimano 105 vs Tiagra", "Twelve speeds or ten."};
constexpr Page kGroceries = {"https://list.example/groceries",
                             "Weekly groceries", "Oat milk, lemons, rice."};
constexpr Page kMail = {"https://mail.example/", "Inbox", "Mail."};
constexpr Page kCalendar = {"https://calendar.example/", "Calendar", "Today."};
constexpr Page kChat = {"https://chat.example/", "Chat", "Messages."};
constexpr Page kDocs = {"https://docs.example/", "Docs", "Documents."};
constexpr Page kLaunch = {
    "https://docs.example/launch-checklist", "Launch checklist",
    "Everything that has to be true before Thursday. Pricing is final, the "
    "help center articles are written, and support knows the new plan names. "
    "The last open item is the status page."};
constexpr Page kPricing = {"https://docs.example/pricing-draft",
                           "Pricing page, draft 3", "Three plans."};
constexpr Page kCompetitors = {"https://docs.example/competitor-pricing",
                               "Competitor pricing notes", "Five products."};
constexpr Page kFailedRun = {"https://ci.example/runs/4821",
                             "Run #4821 failed: browser tests",
                             "One test timed out."};
constexpr Page kRetries = {"https://docs.example/retry-policy",
                           "Retry policy for flaky tests", "Retry once."};
constexpr Page kStandup = {"https://docs.example/standup",
                           "Standup notes, Tuesday", "Three updates."};

constexpr const Page* kPages[] = {
    &kLisbon,      &kTram,      &kFlights, &kStays,  &kPastry,    &kSearch,
    &kAsync,       &kPin,       &kSelect,  &kSizing, &kGroupsets, &kGroceries,
    &kMail,        &kCalendar,  &kChat,    &kDocs,   &kLaunch,    &kPricing,
    &kCompetitors, &kFailedRun, &kRetries, &kStandup};

constexpr char kJevAddress[] = "https://api.typesafe.ai/v1";

const Site* SiteOf(const GURL& url) {
  for (const Site& site : kSites) {
    if (url.host() == site.host) {
      return &site;
    }
  }
  return nullptr;
}

std::string PageHtml(const Site& site, const Page& page) {
  return base::StrCat({
      "<!doctype html><meta charset=utf-8><title>",
      page.title,
      "</title><link rel=icon href=/favicon.svg><style>"
      ":root{color-scheme:light dark;--accent:",
      site.color,
      "}body{margin:0;font:18px/1.65 -apple-system,sans-serif;color:#1f2328;"
      "background:#fff}nav{display:flex;align-items:center;gap:10px;"
      "padding:20px 56px;font-weight:600}nav i{width:26px;height:26px;"
      "border-radius:7px;background:var(--accent)}header{margin:0 40px;"
      "padding:120px 56px 48px;border-radius:24px;color:#fff;background:"
      "linear-gradient(135deg,var(--accent),#F9C784)}h1{margin:0;"
      "max-width:16em;font-size:52px;line-height:1.1;letter-spacing:-.02em}"
      "main{max-width:40em;padding:36px 96px;color:#4b5563}"
      "@media(prefers-color-scheme:dark){body{background:#151515;"
      "color:#eee}main{color:#a3a3a3}}</style><nav><i></i>",
      site.name,
      "</nav><header><h1>",
      page.title,
      "</h1></header><main><p>",
      page.text,
      "</main>",
  });
}

std::string FaviconSvg(const Site& site) {
  return base::StrCat({
      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
      "<rect width='32' height='32' rx='8' fill='",
      site.color,
      "'/><text x='16' y='22' text-anchor='middle' fill='#fff' "
      "font-family='-apple-system,Helvetica' font-weight='700' font-size='",
      site.letter.size() > 1 ? "13" : "18",
      "'>",
      site.letter,
      "</text></svg>",
  });
}

std::string BodyOf(const network::ResourceRequest& request) {
  std::string body;
  if (request.request_body) {
    for (const network::DataElement& element :
         *request.request_body->elements()) {
      if (element.type() == network::DataElement::Tag::kBytes) {
        body += element.As<network::DataElementBytes>().AsStringPiece();
      }
    }
  }
  return body;
}

void Respond(std::string_view type,
             const std::string& body,
             network::mojom::URLLoaderClient* client) {
  content::URLLoaderInterceptor::WriteResponse(
      base::StrCat({"HTTP/1.1 200 OK\nContent-Type: ", type, "\n\n"}), body,
      client);
}

// Draws the README's pictures of uw. Every page and model provider is played
// here, so nothing leaves the machine.
//
//   cd .chromium/src
//   UW_SCREENSHOT_DIR=../../docs/assets/screenshots ./out/uw/browser_tests \
//       --gtest_filter='ReadmeScreenshotsBrowserTest.*'
class ReadmeScreenshotsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(::switches::kScreenInfo,
                                    "{0,0 1600x1000 devicePixelRatio=2}");
  }

  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    // An empty folder stands in for ~/.agents/skills and the connector list.
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    SetAgentsSkillsFolderForTesting(home_.GetPath().AppendASCII("skills"));
    Connectors::SetUpForTesting(home_.GetPath().AppendASCII("connectors"),
                                base::FilePath("/bin/sh"));
    interceptor_ = std::make_unique<content::URLLoaderInterceptor>(
        base::BindLambdaForTesting(
            [this](content::URLLoaderInterceptor::RequestParams* params) {
              return Serve(params);
            }));
  }

  void TearDownOnMainThread() override {
    interceptor_.reset();
    SetAgentsSkillsFolderForTesting(base::FilePath());
    Connectors::SetUpForTesting(base::FilePath(), base::FilePath());
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  bool Serve(content::URLLoaderInterceptor::RequestParams* params) {
    const network::ResourceRequest& request = params->url_request;
    const GURL& url = request.url;
    network::mojom::URLLoaderClient* client = params->client.get();
    if (const auto models = models_.find(url.host());
        url.path() == "/v1/models" && models != models_.end()) {
      Respond("application/json", models->second, client);
      return true;
    }
    if (url.spec() == base::StrCat({kJevAddress, "/systemone"})) {
      const std::string body = BodyOf(request);
      const bool about_move =
          !moving_title_.empty() &&
          body.find(base::StrCat({"Title: ", moving_title_, "\\n"})) !=
              std::string::npos;
      Respond("application/json",
              SystemOneAnswer(about_move ? moving_to_ : "none", 0.97), client);
      return true;
    }
    const Site* site = SiteOf(url);
    if (!site) {
      return false;
    }
    if (url.path() == "/favicon.svg") {
      Respond("image/svg+xml", FaviconSvg(*site), client);
      return true;
    }
    for (const Page* page : kPages) {
      if (GURL(page->url) == url) {
        Respond("text/html", PageHtml(*site, *page), client);
        return true;
      }
    }
    return false;
  }

  PrefService& prefs() { return *browser()->GetProfile()->GetPrefs(); }
  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  void SetScheme(ThemeService::BrowserColorScheme scheme) {
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetBrowserColorScheme(scheme);
  }

  // Saves `colors` as the profile's theme, as the picker does.
  void SetTheme(std::vector<SkColor> colors) {
    Theme theme;
    theme.colors = std::move(colors);
    WriteTheme(prefs(), theme);
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetUserColorAndBrowserColorVariant(
            theme.colors[0], ui::mojom::BrowserColorVariant::kTonalSpot);
  }

  void NameWorkspace(const std::string& name, const std::string& icon) {
    const std::string& id = WorkspaceWindow::From(browser())->workspace();
    WorkspaceList::Get().Rename(id, name);
    WorkspaceList::Get().SetIcon(id, icon);
  }

  void AddWorkspace(const std::string& name, const std::string& icon) {
    WorkspaceList::Get().Add(name, icon,
                             browser()->GetProfile()->GetBaseName());
  }

  // Shows `page` in the window's first tab.
  tabs::TabInterface* BrowseFirst(const Page& page) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(page.url)));
    return tabs()->GetActiveTab();
  }

  // Opens `page` in a new tab, as typing its address does, or as a link in
  // `source` does.
  tabs::TabInterface* Open(const Page& page,
                           tabs::TabInterface* source = nullptr,
                           bool front = true) {
    NavigateParams params(
        browser(), GURL(page.url),
        source ? ui::PAGE_TRANSITION_LINK : ui::PAGE_TRANSITION_TYPED);
    params.disposition = front ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                               : WindowOpenDisposition::NEW_BACKGROUND_TAB;
    if (source) {
      params.source_contents = source->GetContents();
    }
    Navigate(&params);
    content::WebContents* contents = params.navigated_or_inserted_contents;
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(contents);
  }

  void Pin(const Page& page) {
    tabs::TabInterface* tab = Open(page);
    tabs()->SetTabPinned(tabs()->GetIndexOfTab(tab), true);
  }

  tab_groups::TabGroupId Task(std::vector<tabs::TabInterface*> tops,
                              const std::u16string& name) {
    std::vector<PageId> ids;
    for (tabs::TabInterface* tab : tops) {
      ids.push_back(controller()->IdFor(tab).value());
    }
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask(ids, name);
    CHECK(task);
    base::RunLoop().RunUntilIdle();
    return *task;
  }

  void Activate(tabs::TabInterface* tab) {
    tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tab));
    base::RunLoop().RunUntilIdle();
  }

  // Saves a provider made from `preset` with a key, and returns its id.
  std::string AddPreset(ProviderPreset preset, const std::string& key) {
    Provider provider = PresetProvider(preset);
    provider.id = AddProvider(prefs(), provider);
    if (!key.empty()) {
      provider.key_account = KeyAccountFor(provider.id);
      EXPECT_TRUE(ApiKeyStore().SetKey(provider.key_account, key));
      UpdateProvider(prefs(), provider);
    }
    return provider.id;
  }

  OrganizeBar* Bar(views::View* view) {
    if (auto* bar = views::AsViewClass<OrganizeBar>(view)) {
      return bar;
    }
    for (views::View* child : view->children()) {
      if (OrganizeBar* bar = Bar(child)) {
        return bar;
      }
    }
    return nullptr;
  }

  // Each host's answer to a model list.
  std::map<std::string, std::string, std::less<>> models_;
  // Jev files the tab titled `moving_title_` into the task `moving_to_`.
  std::string moving_title_;
  std::string moving_to_;

 private:
  base::ScopedTempDir home_;
  ScopedMemoryKeychain keychain_;
  std::unique_ptr<content::URLLoaderInterceptor> interceptor_;
};

IN_PROC_BROWSER_TEST_F(ReadmeScreenshotsBrowserTest, JevFilesATab) {
  prefs().SetBoolean(kOrganizationEnabledPref, false);
  SetScheme(ThemeService::BrowserColorScheme::kLight);
  SetTheme({SkColorSetRGB(0xE4, 0xDC, 0xF7), SkColorSetRGB(0xFB, 0xE3, 0xD3)});
  NameWorkspace("Personal", "🏡");

  tabs::TabInterface* lisbon = BrowseFirst(kLisbon);
  tabs::TabInterface* flights = Open(kFlights);
  tabs::TabInterface* stays = Open(kStays);
  tabs::TabInterface* async = Open(kAsync);
  tabs::TabInterface* select = Open(kSelect);
  tabs::TabInterface* sizing = Open(kSizing);
  tabs::TabInterface* groupsets = Open(kGroupsets);
  Open(kGroceries);
  tabs::TabInterface* search = Open(kSearch);
  Open(kTram, lisbon, /*front=*/false);
  Open(kPin, async, /*front=*/false);
  const tab_groups::TabGroupId trip =
      Task({lisbon, flights, stays}, u"Plan Lisbon trip");
  Task({async, select}, u"Learn async Rust");
  Task({sizing, groupsets}, u"Buy a road bike");
  Activate(search);

  models_["api.typesafe.ai"] = R"({"data":[{"id":"jev-latest"}]})";
  const std::string jev = AddPreset(ProviderPreset::kJev, "jev-key");
  SetChoice(prefs(), ModelSlot::kDecision, jev, "jev-latest");
  moving_title_ = std::string(kPastry.title);
  moving_to_ = trip.ToString();
  prefs().SetBoolean(kOrganizationEnabledPref, true);

  tabs::TabInterface* pastry = Open(kPastry, search);
  AutomaticOrganizer::From(browser())->DispatchNowForTesting();
  OrganizeBar* bar = Bar(BrowserView::GetBrowserViewForBrowser(browser()));
  ASSERT_TRUE(bar);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return pastry->GetGroup() == trip && bar->card_visible() &&
           AutomaticOrganizer::From(browser())->pending_requests() == 0;
  }));
  EXPECT_EQ(bar->card()->headline(), u"Moved to Plan Lisbon trip");
  SaveWindowScreenshot(browser(), "organize");
}

IN_PROC_BROWSER_TEST_F(ReadmeScreenshotsBrowserTest, BringYourOwnKey) {
  SetScheme(ThemeService::BrowserColorScheme::kLight);
  SetTheme({SkColorSetRGB(0xE4, 0xDC, 0xF7), SkColorSetRGB(0xFB, 0xE3, 0xD3)});
  NameWorkspace("Personal", "🏡");
  models_["api.openai.com"] =
      R"({"data":[{"id":"gpt-5"},{"id":"gpt-5-mini"},{"id":"gpt-5-nano"}]})";
  models_["api.anthropic.com"] =
      R"({"data":[{"id":"claude-opus-5-5"},{"id":"claude-sonnet-5-5"},)"
      R"({"id":"claude-haiku-5-5"}]})";
  models_["localhost"] = R"({"data":[{"id":"qwen3:8b"},{"id":"llama3.2:3b"}]})";
  models_["api.typesafe.ai"] = R"({"data":[{"id":"jev-latest"}]})";

  AddPreset(ProviderPreset::kOpenAi, "sk-openai");
  const std::string anthropic =
      AddPreset(ProviderPreset::kAnthropic, "sk-anthropic");
  AddPreset(ProviderPreset::kOllama, "");
  const std::string jev = AddPreset(ProviderPreset::kJev, "jev-key");
  SetChoice(prefs(), ModelSlot::kLanguage, anthropic, "claude-haiku-5-5");
  SetChoice(prefs(), ModelSlot::kDecision, jev, "jev-latest");
  SetChoice(prefs(), ModelSlot::kAgent, anthropic, "claude-sonnet-5-5");

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://ai/providers")));
  ASSERT_EQ(true, content::EvalJs(tabs()->GetActiveWebContents(), R"(
    new Promise((resolve) => {
      const check = () => document.body.innerText.includes('1 model') &&
              document.body.innerText.includes('2 models')
          ? resolve(true) : setTimeout(check, 20);
      check();
    }))"));
  SaveWindowScreenshot(browser(), "bring-your-own-key");
}

IN_PROC_BROWSER_TEST_F(ReadmeScreenshotsBrowserTest, WorkspacesAndPins) {
  SetScheme(ThemeService::BrowserColorScheme::kDark);
  NameWorkspace("Work", "💼");
  AddWorkspace("Personal", "🏡");
  AddWorkspace("Side project", "🚀");

  BrowseFirst(kMail);
  tabs()->SetTabPinned(0, true);
  Pin(kCalendar);
  Pin(kChat);
  Pin(kDocs);
  tabs::TabInterface* launch = Open(kLaunch);
  tabs::TabInterface* pricing = Open(kPricing);
  tabs::TabInterface* run = Open(kFailedRun);
  tabs::TabInterface* retries = Open(kRetries);
  Open(kStandup);
  Open(kCompetitors, pricing, /*front=*/false);
  Task({launch, pricing}, u"Ship the new pricing");
  Task({run, retries}, u"Fix flaky CI");
  Activate(launch);
  SaveWindowScreenshot(browser(), "workspaces");
}

}  // namespace
}  // namespace uw
