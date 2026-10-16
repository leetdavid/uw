// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/ai_providers/catalog.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The shape of https://models.dev/api.json, cut down.
constexpr char kApiJson[] = R"({
  "openai": {"models": {
    "gpt-5": {"tool_call": true, "reasoning": true,
              "reasoning_options": [{"type": "effort",
                                     "values": ["minimal", "low", "medium", "high"]}]},
    "gpt-4o": {"tool_call": true, "reasoning": false}
  }},
  "anthropic": {"models": {
    "claude-opus-5-5": {"tool_call": true, "reasoning": true,
                        "reasoning_options": [{"type": "effort",
                                               "values": ["low", "medium", "high", "xhigh", "max"]}]},
    "claude-sonnet-4-5": {"tool_call": true, "reasoning": true,
                          "reasoning_options": [{"type": "budget_tokens", "min": 1024}]}
  }},
  "openrouter": {"models": {
    "x/switch-only": {"tool_call": true, "reasoning": true,
                      "reasoning_options": [{"type": "toggle"}]},
    "sao10k/l3.3-euryale-70b": {"tool_call": false, "reasoning": false}
  }},
  "deepseek": {"api": "https://api.deepseek.com", "models": {
    "deepseek-v4-pro": {"tool_call": true, "reasoning": true,
                        "reasoning_options": [{"type": "toggle"},
                                              {"type": "effort",
                                               "values": ["low", "high", "max"]}]}
  }},
  "zai": {"api": "https://api.z.ai/api/paas/v4", "models": {
    "glm-5": {"tool_call": true, "reasoning_options":
                  [{"type": "effort", "values": ["high"]}]}
  }},
  "zai-coding-plan": {"api": "https://api.z.ai/api/coding/paas/v4", "models": {
    "glm-5": {"tool_call": false}
  }},
  "groq": {"models": {"llama-4": {"tool_call": true}}}
})";

Provider Preset(ProviderPreset preset) {
  return PresetProvider(preset);
}

Provider Custom(std::string address) {
  Provider provider = PresetProvider(ProviderPreset::kCustom);
  provider.address = std::move(address);
  return provider;
}

TEST(CatalogTest, NamedLevelsAndToolCallsComeFromTheCatalog) {
  const std::optional<ModelCatalog> catalog =
      ModelCatalog::FromModelsDev(kApiJson);
  ASSERT_TRUE(catalog);

  const ModelAbilities gpt5 =
      catalog->For(Preset(ProviderPreset::kOpenAi), "gpt-5");
  EXPECT_TRUE(gpt5.calls_tools);
  EXPECT_THAT(gpt5.efforts, ElementsAre("minimal", "low", "medium", "high"));
  EXPECT_THAT(catalog->For(Preset(ProviderPreset::kOpenAi), "gpt-4o").efforts,
              IsEmpty());
  EXPECT_THAT(
      catalog->For(Preset(ProviderPreset::kAnthropic), "claude-opus-5-5")
          .efforts,
      ElementsAre("low", "medium", "high", "xhigh", "max"));
  EXPECT_FALSE(
      catalog
          ->For(Preset(ProviderPreset::kOpenRouter), "sao10k/l3.3-euryale-70b")
          .calls_tools);
}

TEST(CatalogTest, ABudgetOrASwitchIsNoLevels) {
  const std::optional<ModelCatalog> catalog =
      ModelCatalog::FromModelsDev(kApiJson);
  ASSERT_TRUE(catalog);
  EXPECT_THAT(
      catalog->For(Preset(ProviderPreset::kAnthropic), "claude-sonnet-4-5")
          .efforts,
      IsEmpty());
  EXPECT_THAT(catalog->For(Preset(ProviderPreset::kOpenRouter), "x/switch-only")
                  .efforts,
              IsEmpty());
}

TEST(CatalogTest, AnUnknownModelGetsItsFormatsUsualLevels) {
  const std::optional<ModelCatalog> catalog =
      ModelCatalog::FromModelsDev(kApiJson);
  ASSERT_TRUE(catalog);
  // Chat completions gets the usual three; Messages and Responses get none.
  Provider custom = Preset(ProviderPreset::kCustom);
  EXPECT_TRUE(CatalogCovers(custom.preset));
  EXPECT_TRUE(CatalogCovers(ProviderPreset::kOpenRouter));
  EXPECT_FALSE(CatalogCovers(ProviderPreset::kOllama));
  const ModelAbilities proxy = catalog->For(custom, "deepseek-flash");
  EXPECT_TRUE(proxy.calls_tools);
  EXPECT_THAT(proxy.efforts, ElementsAre("low", "medium", "high"));
  EXPECT_THAT(
      catalog->For(Preset(ProviderPreset::kOpenAi), "gpt-9-preview").efforts,
      IsEmpty());
  EXPECT_THAT(
      catalog->For(Preset(ProviderPreset::kAnthropic), "claude-new").efforts,
      IsEmpty());
  EXPECT_TRUE(catalog->For(Preset(ProviderPreset::kAnthropic), "claude-new")
                  .calls_tools);
}

// A Custom provider reads the catalog provider at its address: the same
// address, else the only one on the same host.
TEST(CatalogTest, ACustomProviderReadsTheCatalogAtItsAddress) {
  const std::optional<ModelCatalog> catalog =
      ModelCatalog::FromModelsDev(kApiJson);
  ASSERT_TRUE(catalog);
  for (const char* address :
       {"https://api.deepseek.com", "https://api.deepseek.com/",
        "https://API.deepseek.com/v1"}) {
    EXPECT_THAT(catalog->For(Custom(address), "deepseek-v4-pro").efforts,
                ElementsAre("low", "high", "max"))
        << address;
  }
  // A model its provider's entry does not list gets the usual levels.
  EXPECT_THAT(
      catalog->For(Custom("https://api.deepseek.com"), "deepseek-next").efforts,
      ElementsAre("low", "medium", "high"));

  // Two catalog providers share api.z.ai, so only a full address matches.
  EXPECT_THAT(
      catalog->For(Custom("https://api.z.ai/api/paas/v4/"), "glm-5").efforts,
      ElementsAre("high"));
  EXPECT_FALSE(
      catalog->For(Custom("https://api.z.ai/api/coding/paas/v4"), "glm-5")
          .calls_tools);
  EXPECT_THAT(catalog->For(Custom("https://api.z.ai/v1"), "glm-5").efforts,
              ElementsAre("low", "medium", "high"));

  EXPECT_THAT(
      catalog->For(Custom("http://localhost:4000/v1"), "deepseek-v4-pro")
          .efforts,
      ElementsAre("low", "medium", "high"));
}

TEST(CatalogTest, TheSavedCopyReadsBackTheSame) {
  const std::optional<ModelCatalog> catalog =
      ModelCatalog::FromModelsDev(kApiJson);
  ASSERT_TRUE(catalog);
  const std::optional<ModelCatalog> saved =
      ModelCatalog::FromSaved(catalog->ToSaved());
  ASSERT_TRUE(saved);
  EXPECT_THAT(saved->For(Preset(ProviderPreset::kOpenAi), "gpt-5").efforts,
              ElementsAre("minimal", "low", "medium", "high"));
  EXPECT_THAT(
      saved->For(Custom("https://api.deepseek.com"), "deepseek-v4-pro").efforts,
      ElementsAre("low", "high", "max"));
  // A provider with no address and no preset is left out.
  EXPECT_EQ(catalog->ToSaved().find("groq"), std::string::npos);
}

TEST(CatalogTest, WhatIsNotACatalogIsRefused) {
  EXPECT_FALSE(ModelCatalog::FromModelsDev("not json"));
  EXPECT_FALSE(ModelCatalog::FromModelsDev(R"({"deepseek": {"models": {}}})"));
  EXPECT_FALSE(ModelCatalog::FromSaved(R"({"version": 0, "models": {}})"));
}

}  // namespace
}  // namespace uw
