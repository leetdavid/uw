// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/skills.h"

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

TEST(SkillsTest, ReadsANameAndADescription) {
  const std::optional<SkillFrontMatter> front = ReadSkillFrontMatter(
      "---\nname: book-activities\ndescription: Books tours and "
      "tickets.\n---\n\nCompare the sites in the user's history.\n");

  ASSERT_TRUE(front);
  EXPECT_EQ(front->name, "book-activities");
  EXPECT_EQ(front->description, "Books tours and tickets.");
  EXPECT_FALSE(front->made_in_chat);
}

TEST(SkillsTest, ReadsQuotedAndMultiLineDescriptions) {
  EXPECT_EQ(ReadSkillFrontMatter(
                "---\nname: a\ndescription: \"Says \\\"hi\\\" twice\"\n---\n")
                ->description,
            "Says \"hi\" twice");
  EXPECT_EQ(ReadSkillFrontMatter("---\nname: a\ndescription: 'It''s here'\n"
                                 "---\n")
                ->description,
            "It's here");
  EXPECT_EQ(ReadSkillFrontMatter("---\r\nname: a\r\ndescription: >\r\n  Books "
                                 "tours\r\n  and tickets.\r\n---\r\n")
                ->description,
            "Books tours and tickets.");
  EXPECT_EQ(ReadSkillFrontMatter("---\nname: a\ndescription: Books tours\n"
                                 "  and tickets.\nlicense: MIT\n---\n")
                ->description,
            "Books tours and tickets.");
}

TEST(SkillsTest, RefusesAFileWithoutANameOrADescription) {
  EXPECT_FALSE(ReadSkillFrontMatter("# Book activities\n"));
  EXPECT_FALSE(ReadSkillFrontMatter("---\nname: a\n---\n"));
  EXPECT_FALSE(ReadSkillFrontMatter("---\ndescription: b\n---\n"));
  EXPECT_FALSE(ReadSkillFrontMatter("---\nname: a\ndescription: b\n"));
}

TEST(SkillsTest, ReadsWhoMayStartASkill) {
  const std::optional<SkillFrontMatter> handoff = ReadSkillFrontMatter(
      "---\nname: handoff\ndescription: Hands off.\nargument-hint: \"What "
      "will the next session be used for?\"\ndisable-model-invocation: "
      "true\n---\n");
  ASSERT_TRUE(handoff);
  EXPECT_FALSE(handoff->for_agent);
  EXPECT_TRUE(handoff->for_user);
  EXPECT_EQ(handoff->argument_hint, "What will the next session be used for?");

  const std::optional<SkillFrontMatter> manners = ReadSkillFrontMatter(
      "---\nname: site-manners\ndescription: Acts politely.\n"
      "user-invocable: False\n---\n");
  ASSERT_TRUE(manners);
  EXPECT_TRUE(manners->for_agent);
  EXPECT_FALSE(manners->for_user);

  const std::optional<SkillFrontMatter> both = ReadSkillFrontMatter(
      "---\nname: a\ndescription: b\ndisable-model-invocation: false\n---\n");
  EXPECT_TRUE(both->for_agent);
  EXPECT_TRUE(both->for_user);
  EXPECT_EQ(both->argument_hint, "");
}

TEST(SkillsTest, AStartedSkillTakesWhatTheUserTyped) {
  EXPECT_EQ(StartedSkillMessage("Write it for: $ARGUMENTS.", "T137"),
            "Write it for: T137.");
  EXPECT_EQ(StartedSkillMessage("Hand off the work.", "T137 next"),
            "Hand off the work.\n\nT137 next");
  EXPECT_EQ(StartedSkillMessage("Hand off the work.", ""),
            "Hand off the work.");
}

TEST(SkillsTest, InstructionsFollowTheFrontMatter) {
  EXPECT_EQ(SkillInstructions("---\nname: a\ndescription: b\n---\n\n# Steps\n"
                              "1. Compare.\n"),
            "# Steps\n1. Compare.");
  EXPECT_EQ(SkillInstructions("Just text.\n"), "Just text.");
}

TEST(SkillsTest, AcceptsOnlyPlainNames) {
  EXPECT_TRUE(IsSkillName("book-activities2"));
  EXPECT_FALSE(IsSkillName(""));
  EXPECT_FALSE(IsSkillName("Book"));
  EXPECT_FALSE(IsSkillName("../evil"));
  EXPECT_FALSE(IsSkillName(std::string(65, 'a')));
}

TEST(SkillsTest, FindsTheSkillsInAFolder) {
  base::ScopedTempDir folder;
  ASSERT_TRUE(folder.CreateUniqueTempDir());
  const auto write = [&](const char* name, const char* text) {
    const base::FilePath skill = folder.GetPath().AppendASCII(name);
    ASSERT_TRUE(base::CreateDirectory(skill));
    ASSERT_TRUE(base::WriteFile(skill.AppendASCII("SKILL.md"), text));
  };
  write("weekly-groceries",
        "---\nname: weekly-groceries\ndescription: Orders groceries.\n---\n");
  write("book-activities",
        "---\nname: book-activities\ndescription: Books tours.\n---\n");
  write("notes", "No front matter.\n");

  const std::vector<Skill> skills =
      FindSkills(folder.GetPath(), /*agents_folder=*/true);

  ASSERT_EQ(skills.size(), 2u);
  EXPECT_EQ(skills[0].name, "book-activities");
  EXPECT_EQ(skills[0].source, Skill::Source::kAgents);
  EXPECT_EQ(
      skills[0].file,
      folder.GetPath().AppendASCII("book-activities").AppendASCII("SKILL.md"));
  EXPECT_EQ(skills[1].name, "weekly-groceries");
}

TEST(SkillsTest, ASkillWrittenInAChatReadsBack) {
  base::ScopedTempDir folder;
  ASSERT_TRUE(folder.CreateUniqueTempDir());

  ASSERT_TRUE(WriteSkill(folder.GetPath(), "book-activities",
                         "Books \"tours\" and\ntickets.",
                         "Compare the sites.\n"));

  const std::vector<Skill> skills =
      FindSkills(folder.GetPath(), /*agents_folder=*/false);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].source, Skill::Source::kChat);
  EXPECT_EQ(skills[0].description, "Books \"tours\" and tickets.");
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(skills[0].file, &text));
  EXPECT_EQ(SkillInstructions(text), "Compare the sites.");
  EXPECT_FALSE(WriteSkill(folder.GetPath(), "book-activities", "Again.", "x"));
  EXPECT_FALSE(WriteSkill(folder.GetPath(), "../evil", "Bad.", "x"));
}

}  // namespace
}  // namespace uw
