// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <string>

#include "base/feature_list.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_delegate.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_util.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/core/common/autofill_debug_features.h"
#include "components/download/public/common/download_item.h"
#include "components/network_time/network_time_tracker.h"
#include "components/omnibox/browser/aim_eligibility_service_features.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/search_engines/search_engine_type.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/unified_consent/pref_names.h"
#include "content/public/browser/download_manager.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "google_apis/google_api_keys.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace uw {
namespace {

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

std::unique_ptr<net::test_server::HttpResponse> ServeZip(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/uw-check.zip") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("application/zip");
  response->AddCustomHeader("Content-Disposition",
                            "attachment; filename=\"uw-check.zip\"");
  response->set_content(std::string("PK\x05\x06", 4) + std::string(18, '\0'));
  return response;
}

// What uw does instead of sending Google what the user does, per
// docs/plans/google-requests.md.
class GoogleRequestsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  bool Shown(const std::string& page, const std::string& selector) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(page)));
    return content::EvalJs(
               browser()->tab_strip_model()->GetActiveWebContents(),
               std::string(kFind) + "(async () => (await until('" + selector +
                   "')).checkVisibility())()")
        .ExtractBool();
  }
};

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       TheFeaturesThatTellGoogleWhatYouDoAreOff) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(
      autofill::features::debug::kAutofillServerCommunication));
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(network_time::kNetworkTimeServiceQuerying));
  EXPECT_FALSE(base::FeatureList::IsEnabled(omnibox::kAimEnabled));
  EXPECT_FALSE(base::FeatureList::IsEnabled(features::kPreconnectToSearch));
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       GoogleSearchesCarryNoAddressBarStats) {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  search_test_utils::WaitForTemplateURLServiceToLoad(service);
  const TemplateURL* google = service->GetDefaultSearchProvider();
  ASSERT_TRUE(google);
  ASSERT_EQ(google->GetEngineType(service->search_terms_data()),
            SEARCH_ENGINE_GOOGLE);

  TemplateURLRef::SearchTermsArgs args(u"uw browser");
  args.original_query = u"uw b";
  args.accepted_suggestion = 1;
  args.searchbox_stats.set_client_name("chrome");
  args.searchbox_stats.add_available_suggestions()->set_index(0);
  const GURL search(google->url_ref().ReplaceSearchTerms(
      args, service->search_terms_data()));

  EXPECT_NE(search.query().find("q=uw+browser"), std::string::npos)
      << search;
  EXPECT_EQ(search.query().find("gs_lcrp="), std::string::npos) << search;
  EXPECT_EQ(search.query().find("oq="), std::string::npos) << search;
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       ADownloadInAPrivateWindowIsNotScannedWithoutAKey) {
  base::ScopedTempDir downloads;
  {
    base::ScopedAllowBlockingForTesting blocking;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
  }
  browser()->GetProfile()->GetPrefs()->SetFilePath(
      prefs::kDownloadDefaultDirectory, downloads.GetPath());
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeZip));
  ASSERT_TRUE(embedded_test_server()->Start());

  Browser* incognito = CreateIncognitoBrowser();
  ui_test_utils::DownloadURL(incognito,
                             embedded_test_server()->GetURL("/uw-check.zip"));
  content::DownloadManager::DownloadVector items;
  incognito->GetProfile()->GetDownloadManager()->GetAllDownloads(&items);
  ASSERT_EQ(items.size(), 1u);
  download::DownloadItem* item = items[0];

  safe_browsing::DownloadProtectionDelegate* protection =
      g_browser_process->safe_browsing_service()
          ->download_protection_service()
          ->delegate();
  // Everything else would let Safe Browsing scan this download.
  ASSERT_TRUE(
      safe_browsing::IsSafeBrowsingEnabled(*incognito->GetProfile()->GetPrefs()));
  ASSERT_NE(protection->IsSupportedDownload(*item, item->GetTargetFilePath()),
            safe_browsing::MayCheckDownloadResult::kMayNotCheckDownload);

  EXPECT_FALSE(google_apis::HasAPIKeyConfigured());
  EXPECT_FALSE(protection->MayCheckClientDownload(item));
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       PRE_TheRemovedSettingsComeBackOff) {
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kSafeBrowsingEnhanced, true);
  prefs->SetBoolean(
      unified_consent::prefs::kUrlKeyedAnonymizedDataCollectionEnabled, true);
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       TheRemovedSettingsComeBackOff) {
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  EXPECT_FALSE(prefs->GetBoolean(prefs::kSafeBrowsingEnhanced));
  EXPECT_FALSE(prefs->GetBoolean(
      unified_consent::prefs::kUrlKeyedAnonymizedDataCollectionEnabled));
  EXPECT_FALSE(safe_browsing::IsEnhancedProtectionEnabled(*prefs));
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       SettingsOfferNeitherRemovedSetting) {
  EXPECT_TRUE(Shown("chrome://settings/security", "#safeBrowsingStandard"));
  EXPECT_FALSE(Shown("chrome://settings/security", "#safeBrowsingEnhanced"));
  EXPECT_FALSE(Shown("chrome://settings/syncSetup", "#urlCollectionToggle"));
}

IN_PROC_BROWSER_TEST_F(GoogleRequestsBrowserTest,
                       ThePrivacyGuideSkipsBothRemovedSettings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/privacy/guide")));
  EXPECT_EQ(true, content::EvalJs(
                      browser()->tab_strip_model()->GetActiveWebContents(),
                      std::string(kFind) + R"((async () => {
        const steps = (await until('settings-privacy-guide-page'))
            .privacyGuideStepToComponentsMap_;
        return !steps.get('msbb').isAvailable() &&
            !steps.get('safeBrowsing').isAvailable() &&
            steps.get('cookies').isAvailable();
      })())"));
}

}  // namespace
}  // namespace uw
