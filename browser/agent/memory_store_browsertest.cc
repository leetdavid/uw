// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/memory_store.h"

#include <optional>
#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/test/browser_test.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace uw {
namespace {

using ::testing::HasSubstr;

class MemoryStoreBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  MemoryStore& Store(Profile* profile = nullptr) {
    return *MemoryStore::For(profile ? profile : browser()->GetProfile());
  }

  base::expected<Memory, std::string> Keep(std::string text,
                                           bool core = false,
                                           int64_t replaces = 0,
                                           Profile* profile = nullptr) {
    std::optional<base::expected<Memory, std::string>> kept;
    Store(profile).Keep(
        std::move(text), core, "chat-1", replaces,
        base::BindLambdaForTesting(
            [&](base::expected<Memory, std::string> got) { kept = got; }));
    EXPECT_TRUE(base::test::RunUntil([&] { return kept.has_value(); }));
    return kept.value_or(base::unexpected("no answer"));
  }

  std::vector<std::string> Search(std::string words,
                                  Profile* profile = nullptr) {
    std::optional<std::vector<Memory>> found;
    Store(profile).Search(
        MemorySearchWords(words), 20,
        base::BindLambdaForTesting(
            [&](std::vector<Memory> all) { found = std::move(all); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return found.has_value(); }));
    std::vector<std::string> texts;
    for (const Memory& memory : found.value_or(std::vector<Memory>())) {
      texts.push_back(memory.text);
    }
    return texts;
  }
};

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest,
                       SearchFindsTheMostMatchingWordsFirst) {
  ASSERT_TRUE(Keep("Banks with Chase.").has_value());
  ASSERT_TRUE(Keep("Pays the card bill on the 1st.").has_value());
  ASSERT_TRUE(Keep("Home airport is SFO.").has_value());

  EXPECT_THAT(Search("card bill bank"),
              testing::ElementsAre("Pays the card bill on the 1st.",
                                   "Banks with Chase."));
  EXPECT_THAT(Search("CHASE"), testing::ElementsAre("Banks with Chase."));
  EXPECT_TRUE(Search("passport").empty());
  // No words: every memory, newest first.
  EXPECT_THAT(Search(""), testing::ElementsAre("Home airport is SFO.",
                                               "Pays the card bill on the 1st.",
                                               "Banks with Chase."));
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, AnUnderscoreMatchesOnlyItself) {
  ASSERT_TRUE(Keep("Names files in snake_case.").has_value());
  ASSERT_TRUE(Keep("Names pets in snakeycase.").has_value());

  EXPECT_THAT(Search("snake_case"),
              testing::ElementsAre("Names files in snake_case."));
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, TheSameSentenceIsKeptOnce) {
  const base::expected<Memory, std::string> first = Keep("Banks with Chase.");
  const base::expected<Memory, std::string> again =
      Keep("Banks with Chase.", /*core=*/true);

  ASSERT_TRUE(first.has_value() && again.has_value());
  EXPECT_EQ(again->id, first->id);
  EXPECT_EQ(Search("chase").size(), 1u);
  ASSERT_EQ(Store().core().size(), 1u);
  EXPECT_EQ(Store().core()[0].text, "Banks with Chase.");
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, AtMostThirtyAreCore) {
  for (size_t i = 0; i < kMaxCoreMemories; ++i) {
    ASSERT_TRUE(
        Keep(base::StrCat({"Core fact ", base::NumberToString(i), "."}), true)
            .has_value());
  }
  const base::expected<Memory, std::string> one_more =
      Keep("Goes by Sam.", true);
  ASSERT_FALSE(one_more.has_value());
  EXPECT_THAT(one_more.error(), HasSubstr("already 30 core memories"));
  EXPECT_TRUE(Keep("Goes by Sam.").has_value());

  // Making one not core makes room.
  const int64_t first = Store().core()[0].id;
  ASSERT_TRUE(Keep("Core fact 0.", false, first).has_value());
  EXPECT_TRUE(Keep("Goes by Sam.", true).has_value());
  EXPECT_EQ(Store().core().size(), kMaxCoreMemories);
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, AMemoryIsOneShortSentence) {
  EXPECT_FALSE(Keep("  ").has_value());
  EXPECT_FALSE(Keep(std::string(kMaxMemoryLength + 1, 'a')).has_value());
  const base::expected<Memory, std::string> kept = Keep("Lives in\nOakland.");
  ASSERT_TRUE(kept.has_value());
  EXPECT_EQ(kept->text, "Lives in Oakland.");
  EXPECT_FALSE(Keep("Lives in Berkeley.", false, kept->id + 100).has_value());
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, ForgettingRemovesACoreMemory) {
  const base::expected<Memory, std::string> kept = Keep("Goes by Sam.", true);
  ASSERT_TRUE(kept.has_value());

  std::optional<std::optional<Memory>> forgot;
  Store().Forget(kept->id,
                 base::BindLambdaForTesting(
                     [&](std::optional<Memory> got) { forgot = got; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return forgot.has_value(); }));

  ASSERT_TRUE(*forgot);
  EXPECT_EQ((*forgot)->text, "Goes by Sam.");
  EXPECT_TRUE(Store().core().empty());
  EXPECT_TRUE(Search("sam").empty());
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, EachProfileHasItsOwnMemory) {
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  ASSERT_TRUE(Keep("Banks with Chase.", true).has_value());

  EXPECT_TRUE(Search("chase", &other).empty());
  bool loaded = false;
  Store(&other).WhenLoaded(base::BindLambdaForTesting([&] { loaded = true; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded; }));
  EXPECT_TRUE(Store(&other).core().empty());
}

IN_PROC_BROWSER_TEST_F(MemoryStoreBrowserTest, APrivateProfileKeepsNoMemory) {
  EXPECT_FALSE(MemoryStore::For(browser()->GetProfile()->GetPrimaryOTRProfile(
      /*create_if_needed=*/true)));
}

}  // namespace
}  // namespace uw
