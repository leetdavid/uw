// Copyright 2026 The uw Authors. All rights reserved.

// Every local build is uw dev, a separate app from the installed uw. See
// docs/adr/0028-local-builds-are-uw-dev.md.

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/test/scoped_command_line.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "crypto/apple/keychain_v2.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/uw_command/command_install.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kInstalledUw[] = "uw-ai";
constexpr char kUwDev[] = "uw-ai-dev";

class UwDevBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    // The copy uw dev starts with its first window runs in this test's
    // --use-mock-keychain, so it copies nothing; let it finish first.
    content::RunAllTasksUntilIdle();
  }

  // The user's own uw dev runs without the switch every test has.
  void RunAsTheUsersUwDev() {
    their_run_.emplace();
    their_run_->GetProcessCommandLine()->RemoveSwitch(
        os_crypt::switches::kUseMockKeychain);
  }

  void Save(std::string_view service,
            std::string_view account,
            std::string_view key) {
    ASSERT_EQ(crypto::apple::KeychainV2::GetInstance().AddGenericPassword(
                  service, account, base::as_byte_span(key)),
              errSecSuccess);
  }

  std::optional<std::string> Read(std::string_view service,
                                  std::string_view account) {
    const auto key =
        crypto::apple::KeychainV2::GetInstance().FindGenericPassword(service,
                                                                     account);
    return key.has_value()
               ? std::optional(std::string(key->begin(), key->end()))
               : std::nullopt;
  }

 private:
  ScopedMemoryKeychain keychain_;
  std::optional<base::test::ScopedCommandLine> their_run_;
};

// uw dev saves keys and installs its command beside the installed uw's, so
// neither replaces the other's.
IN_PROC_BROWSER_TEST_F(UwDevBrowserTest, KeysAndTheCommandAreUwDevsOwn) {
  RunAsTheUsersUwDev();
  const std::string openai = KeyAccountFor("openai");
  Save(kInstalledUw, openai, "their-key");

  ASSERT_TRUE(ApiKeyStore().SetKey(openai, "dev-key"));

  EXPECT_EQ(ApiKeyStore().GetKey(openai), "dev-key");
  EXPECT_EQ(Read(kInstalledUw, openai), "their-key");
  EXPECT_EQ(CommandLink().value(), "/usr/local/bin/uw-dev");
}

// The first start copies the installed uw's model keys and leaves its
// connector sign-ins, which a second app refreshing could sign out.
IN_PROC_BROWSER_TEST_F(UwDevBrowserTest, TheFirstStartCopiesOnlyModelKeys) {
  RunAsTheUsersUwDev();
  const std::string openai = KeyAccountFor("openai");
  Save(kInstalledUw, openai, "their-key");
  Save(kInstalledUw, "connector-oauth:linear", "their-sign-in");

  CopyModelKeysFromInstalledUw();

  EXPECT_EQ(ApiKeyStore().GetKey(openai), "their-key");
  EXPECT_FALSE(Read(kUwDev, "connector-oauth:linear"));
}

// Once uw dev has keys of its own, a later start copies nothing.
IN_PROC_BROWSER_TEST_F(UwDevBrowserTest, ALaterStartCopiesNothing) {
  RunAsTheUsersUwDev();
  Save(kUwDev, KeyAccountFor("jev"), "dev-key");
  Save(kInstalledUw, KeyAccountFor("openai"), "their-key");

  CopyModelKeysFromInstalledUw();

  EXPECT_FALSE(Read(kUwDev, KeyAccountFor("openai")));
}

// A test or scripted run neither reads the installed uw's keys nor writes
// uw dev's.
IN_PROC_BROWSER_TEST_F(UwDevBrowserTest, AMockKeychainRunCopiesNothing) {
  Save(kInstalledUw, KeyAccountFor("openai"), "their-key");

  CopyModelKeysFromInstalledUw();

  EXPECT_FALSE(Read(kUwDev, KeyAccountFor("openai")));
}

}  // namespace
}  // namespace uw
