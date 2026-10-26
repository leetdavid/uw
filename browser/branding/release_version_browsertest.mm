// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

#import <Foundation/Foundation.h>

#include <string>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/version/version_ui.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/base/url_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "uw/browser/branding/report_issue.h"
#include "uw/browser/tab_organization/commands.h"

namespace uw {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

class ReleaseVersionBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    base::apple::SetOverrideOuterBundle(nil);
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  // Stands in for an app bundle whose Info.plist carries `version`.
  void UseBundleVersion(const std::string& version) {
    ASSERT_TRUE(bundles_.CreateUniqueTempDir());
    base::FilePath app = bundles_.GetPath().Append("uw.app");
    base::FilePath contents = app.Append("Contents");
    ASSERT_TRUE(base::CreateDirectory(contents));
    NSDictionary* plist =
        @{@"CFBundleShortVersionString" : base::SysUTF8ToNSString(version)};
    ASSERT_TRUE([plist
        writeToURL:base::apple::FilePathToNSURL(contents.Append("Info.plist"))
             error:nil]);
    base::apple::SetOverrideOuterBundle(
        [NSBundle bundleWithURL:base::apple::FilePathToNSURL(app)]);
  }

 private:
  base::ScopedTempDir bundles_;
};

IN_PROC_BROWSER_TEST_F(ReleaseVersionBrowserTest, ReleaseShowsUwAndChromium) {
  UseBundleVersion("0.1.0");

  EXPECT_EQ(ReleaseVersion(), "0.1.0");
  std::string chromium =
      base::StrCat({"Chromium ", version_info::GetVersionNumber()});
  EXPECT_THAT(base::UTF16ToUTF8(VersionUI::GetAnnotatedVersionStringForUi()),
              AllOf(StartsWith("Version 0.1.0 "),
                    HasSubstr(base::StrCat({"(", chromium, ")"}))));
}

IN_PROC_BROWSER_TEST_F(ReleaseVersionBrowserTest, DevelopmentBuildHasNone) {
  UseBundleVersion(std::string(version_info::GetVersionNumber()));

  EXPECT_EQ(ReleaseVersion(), std::nullopt);
  std::string about =
      base::UTF16ToUTF8(VersionUI::GetAnnotatedVersionStringForUi());
  EXPECT_THAT(
      about,
      HasSubstr(base::StrCat({"Version ", version_info::GetVersionNumber()})));
  EXPECT_THAT(about, Not(HasSubstr("(Chromium ")));
}

IN_PROC_BROWSER_TEST_F(ReleaseVersionBrowserTest, NewIssueCarriesTheVersions) {
  UseBundleVersion("0.1.0");

  GURL issue = NewIssueUrl();
  EXPECT_THAT(issue.spec(),
              StartsWith("https://github.com/leetdavid/uw/issues/new?body="));
  std::string body;
  ASSERT_TRUE(net::GetValueForKeyInQuery(issue, "body", &body));
  EXPECT_THAT(body, AllOf(HasSubstr("uw 0.1.0, Chromium "),
                          HasSubstr(version_info::GetVersionNumber()),
                          HasSubstr(", macOS ")));
}

IN_PROC_BROWSER_TEST_F(ReleaseVersionBrowserTest, HelpMenuOpensANewIssue) {
  chrome::ExecuteCommand(browser(), kReportIssue);

  EXPECT_EQ(
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL(),
      NewIssueUrl());
}

}  // namespace
}  // namespace uw
