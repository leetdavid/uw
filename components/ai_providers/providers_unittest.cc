// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/providers.h"

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

class ProvidersTest : public testing::Test {
 protected:
  ProvidersTest() { RegisterProviderPrefs(prefs_.registry()); }

  TestingPrefServiceSimple prefs_;
};

TEST_F(ProvidersTest, APresetGetsItsNameAsIdAndASecondOneANumber) {
  EXPECT_EQ(AddProvider(prefs_, PresetProvider(ProviderPreset::kOpenAi)),
            "openai");
  EXPECT_EQ(AddProvider(prefs_, PresetProvider(ProviderPreset::kOpenAi)),
            "openai-2");
  EXPECT_EQ(AddProvider(prefs_, PresetProvider(ProviderPreset::kCustom)),
            "custom");

  const std::optional<Provider> openai = FindProvider(prefs_, "openai");
  ASSERT_TRUE(openai);
  EXPECT_EQ(openai->name, "OpenAI");
  EXPECT_EQ(openai->address, "https://api.openai.com/v1");
  EXPECT_EQ(openai->format, ProviderFormat::kOpenAiResponses);
  EXPECT_EQ(ReadProviders(prefs_).size(), 3u);
}

TEST_F(ProvidersTest, AnIdIsReusedOnlyAfterItsProviderIsRemoved) {
  AddProvider(prefs_, PresetProvider(ProviderPreset::kOllama));
  AddProvider(prefs_, PresetProvider(ProviderPreset::kOllama));
  RemoveProvider(prefs_, "ollama");

  EXPECT_FALSE(FindProvider(prefs_, "ollama"));
  EXPECT_TRUE(FindProvider(prefs_, "ollama-2"));
  EXPECT_EQ(AddProvider(prefs_, PresetProvider(ProviderPreset::kOllama)),
            "ollama");
}

TEST_F(ProvidersTest, AnUpdateChangesOnlyTheProviderWithItsId) {
  AddProvider(prefs_, PresetProvider(ProviderPreset::kJev));
  Provider custom = PresetProvider(ProviderPreset::kCustom);
  custom.name = "LiteLLM proxy";
  custom.address = "http://localhost:4000/v1";
  custom.id = AddProvider(prefs_, custom);

  custom.key_account = KeyAccountFor(custom.id);
  EXPECT_TRUE(UpdateProvider(prefs_, custom));
  custom.id = "nobody";
  EXPECT_FALSE(UpdateProvider(prefs_, custom));

  EXPECT_EQ(FindProvider(prefs_, "custom")->key_account, "provider/custom");
  EXPECT_EQ(FindProvider(prefs_, "jev")->key_account, "");
}

TEST_F(ProvidersTest, AnEntryThatCannotBeReadIsSkipped) {
  base::ListValue list;
  list.Append(base::DictValue().Set("id", "broken").Set("preset", 99));
  list.Append("not a provider");
  prefs_.SetList(kProvidersPref, std::move(list));
  AddProvider(prefs_, PresetProvider(ProviderPreset::kAnthropic));

  ASSERT_EQ(ReadProviders(prefs_).size(), 1u);
  EXPECT_EQ(ReadProviders(prefs_)[0].id, "anthropic");
}

TEST_F(ProvidersTest, AChoiceKeepsItsModelsFormat) {
  SetChoice(prefs_, ModelSlot::kLanguage, "custom", "claude-opus-5-5",
            ProviderFormat::kAnthropicMessages);
  EXPECT_EQ(ReadChoice(prefs_, ModelSlot::kLanguage)->format,
            ProviderFormat::kAnthropicMessages);
  SetChoice(prefs_, ModelSlot::kLanguage, "openai", "gpt-5");
  EXPECT_FALSE(ReadChoice(prefs_, ModelSlot::kLanguage)->format);
}

TEST(ProviderAddressTest, PlainHttpStaysOnThisMacOrAPrivateNetwork) {
  for (const char* address :
       {"https://api.openai.com/v1", "http://localhost:8317/v1",
        "http://127.0.0.1:8317/v1", "http://10.0.0.5:8317/v1",
        "http://172.20.1.2/v1", "http://192.168.1.20:8317/v1",
        "http://100.101.102.103:8317/v1", "http://mac-mini.local:8317/v1"}) {
    EXPECT_TRUE(IsValidProviderEndpoint(address)) << address;
  }
  for (const char* address :
       {"http://api.example.com/v1", "http://8.8.8.8/v1",
        "http://172.32.0.1/v1", "http://user:pass@localhost/v1",
        "https://api.example.com/v1#part", "ftp://192.168.1.20/v1",
        "not an address"}) {
    EXPECT_FALSE(IsValidProviderEndpoint(address)) << address;
  }
}

TEST_F(ProvidersTest, AnEffortStaysWithItsModel) {
  SetEffort(prefs_, ModelSlot::kAgent, "high");
  EXPECT_FALSE(ReadChoice(prefs_, ModelSlot::kAgent));

  SetChoice(prefs_, ModelSlot::kAgent, "openai", "gpt-5");
  EXPECT_EQ(ReadChoice(prefs_, ModelSlot::kAgent)->effort, "");
  SetEffort(prefs_, ModelSlot::kAgent, "high");
  EXPECT_EQ(ReadChoice(prefs_, ModelSlot::kAgent)->effort, "high");

  SetChoice(prefs_, ModelSlot::kAgent, "openai", "gpt-6");
  EXPECT_EQ(ReadChoice(prefs_, ModelSlot::kAgent)->effort, "");
}

TEST_F(ProvidersTest, AChoiceOutlivesItsProvider) {
  AddProvider(prefs_, PresetProvider(ProviderPreset::kOpenRouter));
  SetChoice(prefs_, ModelSlot::kAgent, "openrouter", "moonshotai/kimi-k3");
  RemoveProvider(prefs_, "openrouter");

  ASSERT_TRUE(ReadChoice(prefs_, ModelSlot::kAgent));
  EXPECT_FALSE(FindProvider(prefs_, "openrouter"));

  ClearChoice(prefs_, ModelSlot::kAgent);
  EXPECT_FALSE(ReadChoice(prefs_, ModelSlot::kAgent));
}

TEST_F(ProvidersTest, OpenAiServesBothKindsOfModelAndJevOnlyDecisions) {
  const Provider openai = PresetProvider(ProviderPreset::kOpenAi);
  EXPECT_EQ(LanguageFormatOf(openai), ProviderFormat::kOpenAiResponses);
  EXPECT_EQ(DecisionFormatOf(openai), ProviderFormat::kOpenAiDecisions);

  const Provider jev = PresetProvider(ProviderPreset::kJev);
  EXPECT_FALSE(LanguageFormatOf(jev));
  EXPECT_EQ(DecisionFormatOf(jev), ProviderFormat::kSystemOne);

  Provider gateway = PresetProvider(ProviderPreset::kCustom);
  gateway.format = ProviderFormat::kOpenAiDecisions;
  EXPECT_FALSE(LanguageFormatOf(gateway));
  EXPECT_EQ(DecisionFormatOf(gateway), ProviderFormat::kOpenAiDecisions);

  EXPECT_FALSE(DecisionFormatOf(PresetProvider(ProviderPreset::kAnthropic)));
}

TEST_F(ProvidersTest, AQuickChoiceCanBeALanguageModel) {
  SetChoice(prefs_, ModelSlot::kDecision, "openai", "gpt-6.1-terra",
            std::nullopt, /*language=*/true);
  EXPECT_TRUE(ReadChoice(prefs_, ModelSlot::kDecision)->language);
  SetChoice(prefs_, ModelSlot::kDecision, "openai", "gpt-6-luna");
  EXPECT_FALSE(ReadChoice(prefs_, ModelSlot::kDecision)->language);
}

TEST_F(ProvidersTest, GatewaysServeDecisionsInTheirOwnFormats) {
  const Provider vercel = PresetProvider(ProviderPreset::kVercelAiGateway);
  EXPECT_EQ(LanguageFormatOf(vercel), ProviderFormat::kChatCompletions);
  EXPECT_EQ(DecisionFormatOf(vercel), ProviderFormat::kOpenAiDecisions);

  const Provider openrouter = PresetProvider(ProviderPreset::kOpenRouter);
  EXPECT_EQ(LanguageFormatOf(openrouter), ProviderFormat::kChatCompletions);
  EXPECT_EQ(DecisionFormatOf(openrouter), ProviderFormat::kSystemOne);
}

}  // namespace
}  // namespace uw
