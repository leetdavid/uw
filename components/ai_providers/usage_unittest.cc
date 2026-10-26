// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/usage.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "uw/components/ai_providers/formats.h"

namespace uw {
namespace {

TEST(UsageTest, ReadsChatCompletionsUsage) {
  std::optional<ModelUsage> usage = UsageInAnswer(R"({"choices": [],
      "usage": {"prompt_tokens": 1200, "completion_tokens": 80,
                "prompt_tokens_details": {"cached_tokens": 1000},
                "completion_tokens_details": {"reasoning_tokens": 30}}})");
  ASSERT_TRUE(usage);
  EXPECT_EQ(*usage, (ModelUsage{.input = 1200,
                                .cache_read = 1000,
                                .output = 80,
                                .reasoning = 30}));
  EXPECT_FALSE(UsageInAnswer(R"({"choices": []})"));
  EXPECT_FALSE(UsageInAnswer("not json"));
}

TEST(UsageTest, ReadsDeepSeeksCacheAndOpenRoutersCost) {
  EXPECT_EQ(UsageInAnswer(R"({"usage": {"prompt_tokens": 500,
      "completion_tokens": 10, "prompt_cache_hit_tokens": 400,
      "prompt_cache_miss_tokens": 100}})")
                ->cache_read,
            400);
  std::optional<ModelUsage> routed = UsageInAnswer(R"({"usage": {
      "prompt_tokens": 500, "completion_tokens": 10, "cost": 0.0123,
      "prompt_tokens_details": {"cached_tokens": 0, "cache_write_tokens": 450}}})");
  ASSERT_TRUE(routed);
  EXPECT_EQ(routed->cache_write, 450);
  EXPECT_EQ(routed->cost, 0.0123);
  // The provider's own cost wins over the list price.
  EXPECT_EQ(CostOf(*routed, ModelPrice{.input = 3, .output = 15}), 0.0123);
}

TEST(UsageTest, PricesEachKindOfToken) {
  const ModelUsage usage{.input = 1'000'000,
                         .cache_read = 600'000,
                         .cache_write = 100'000,
                         .output = 100'000};
  // 300k fresh at $3, 600k read at $0.30, 100k written at $3.75, 100k out at
  // $15.
  EXPECT_DOUBLE_EQ(*CostOf(usage, ModelPrice{.input = 3,
                                             .output = 15,
                                             .cache_read = 0.3,
                                             .cache_write = 3.75}),
                   0.9 + 0.18 + 0.375 + 1.5);
  // Without cache prices, cached tokens cost as input.
  EXPECT_DOUBLE_EQ(*CostOf(usage, ModelPrice{.input = 1, .output = 2}),
                   1.0 + 0.2);
  EXPECT_FALSE(CostOf(usage, std::nullopt));
}

TEST(UsageTest, KeepsAndReadsBack) {
  const ModelUsage usage{.input = 10, .cache_read = 4, .output = 2, .cost = 0.5};
  EXPECT_EQ(UsageFromValue(UsageToValue(usage)), usage);
  EXPECT_FALSE(UsageFromValue(base::DictValue()));
}

TEST(UsageTest, AnthropicAndResponsesAnswersCarryTheirUsage) {
  std::optional<std::string> anthropic = AnswerFrom(
      ProviderFormat::kAnthropicMessages,
      R"({"content": [{"type": "text", "text": "Hi"}], "stop_reason": "end_turn",
          "usage": {"input_tokens": 20, "cache_read_input_tokens": 900,
                    "cache_creation_input_tokens": 80, "output_tokens": 7}})",
      "https://api.anthropic.com/v1", "claude");
  ASSERT_TRUE(anthropic);
  EXPECT_EQ(UsageInAnswer(*anthropic), (ModelUsage{.input = 1000,
                                                   .cache_read = 900,
                                                   .cache_write = 80,
                                                   .output = 7}));

  std::optional<std::string> responses = AnswerFrom(
      ProviderFormat::kOpenAiResponses,
      R"({"output": [{"type": "message", "content": [
            {"type": "output_text", "text": "Hi"}]}],
          "usage": {"input_tokens": 1000,
                    "input_tokens_details": {"cached_tokens": 800},
                    "output_tokens": 50,
                    "output_tokens_details": {"reasoning_tokens": 40}}})",
      "https://api.openai.com/v1", "gpt");
  ASSERT_TRUE(responses);
  EXPECT_EQ(UsageInAnswer(*responses), (ModelUsage{.input = 1000,
                                                   .cache_read = 800,
                                                   .output = 50,
                                                   .reasoning = 40}));
}

}  // namespace
}  // namespace uw
