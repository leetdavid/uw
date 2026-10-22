// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/ai_providers/formats.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/test/values_test_util.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

constexpr char kAddress[] = "https://api.example.com/v1";
constexpr char kModel[] = "a-model";

// A chat in chat completions' shape, in the middle of the agent's turn:
// instructions, a question, a tool call with what the provider kept, and its
// result.
constexpr char kChat[] = R"({
  "model": "a-model",
  "messages": [
    {"role": "system", "content": "Be brief."},
    {"role": "user", "content": "Open example.com"},
    {"role": "assistant", "content": "",
     "tool_calls": [{"id": "call_1", "type": "function",
                     "function": {"name": "open",
                                  "arguments": "{\"url\":\"https://example.com\"}"}}],
     "uw_kept": {"address": "https://api.example.com/v1", "model": "a-model",
                 "format": FORMAT,
                 "items": [KEPT]}},
    {"role": "tool", "tool_call_id": "call_1", "content": "{\"ok\":true}"}
  ],
  "tools": [{"type": "function",
             "function": {"name": "open", "description": "Opens a page.",
                          "parameters": {"type": "object"}}}]
})";

std::string Chat(int format, const std::string& kept) {
  std::string chat = kChat;
  chat.replace(chat.find("FORMAT"), 6, std::to_string(format));
  chat.replace(chat.find("KEPT"), 4, kept);
  return chat;
}

base::DictValue Parsed(const std::optional<std::string>& body) {
  EXPECT_TRUE(body);
  return base::test::ParseJsonDict(body.value_or("{}"));
}

constexpr char kThinking[] =
    R"({"type": "thinking", "thinking": "Open it.", "signature": "sig"})";
constexpr char kReasoning[] =
    R"({"type": "reasoning", "id": "rs_1", "encrypted_content": "sealed"})";

TEST(FormatsTest, EachFormatHasItsOwnPath) {
  EXPECT_EQ(LanguageModelPath(ProviderFormat::kChatCompletions),
            "/chat/completions");
  EXPECT_EQ(LanguageModelPath(ProviderFormat::kAnthropicMessages), "/messages");
  EXPECT_EQ(LanguageModelPath(ProviderFormat::kOpenAiResponses), "/responses");
}

TEST(FormatsTest, AnthropicGetsBlocksAndItsThinkingBack) {
  const base::DictValue request = Parsed(RequestIn(
      ProviderFormat::kAnthropicMessages,
      Chat(static_cast<int>(ProviderFormat::kAnthropicMessages), kThinking),
      kAddress));

  EXPECT_EQ(request, base::test::ParseJsonDict(R"({
    "model": "a-model",
    "max_tokens": 16000,
    "system": [{"type": "text", "text": "Be brief.",
                "cache_control": {"type": "ephemeral", "ttl": "1h"}}],
    "messages": [
      {"role": "user", "content": [{"type": "text", "text": "Open example.com"}]},
      {"role": "assistant", "content": [
        {"type": "thinking", "thinking": "Open it.", "signature": "sig"},
        {"type": "tool_use", "id": "call_1", "name": "open",
         "input": {"url": "https://example.com"}}]},
      {"role": "user", "content": [
        {"type": "tool_result", "tool_use_id": "call_1", "content": "{\"ok\":true}"}]}
    ],
    "tools": [{"name": "open", "description": "Opens a page.",
               "input_schema": {"type": "object"}}],
    "cache_control": {"type": "ephemeral"}
  })"));
}

TEST(FormatsTest, ResponsesGetsItemsAndItsSealedReasoningBack) {
  const base::DictValue request = Parsed(RequestIn(
      ProviderFormat::kOpenAiResponses,
      Chat(static_cast<int>(ProviderFormat::kOpenAiResponses), kReasoning),
      kAddress));

  EXPECT_EQ(request, base::test::ParseJsonDict(R"({
    "model": "a-model",
    "store": false,
    "include": ["reasoning.encrypted_content"],
    "instructions": "Be brief.",
    "input": [
      {"role": "user", "content": "Open example.com"},
      {"type": "reasoning", "id": "rs_1", "encrypted_content": "sealed"},
      {"type": "function_call", "call_id": "call_1", "name": "open",
       "arguments": "{\"url\":\"https://example.com\"}"},
      {"type": "function_call_output", "call_id": "call_1",
       "output": "{\"ok\":true}"}
    ],
    "tools": [{"type": "function", "name": "open",
               "description": "Opens a page.",
               "parameters": {"type": "object"}}]
  })"));
}

TEST(FormatsTest, KeptPartsGoOnlyBackToWhereTheyCameFrom) {
  // Another provider, another model, or another format never sees them.
  const base::DictValue elsewhere = Parsed(RequestIn(
      ProviderFormat::kAnthropicMessages,
      Chat(static_cast<int>(ProviderFormat::kAnthropicMessages), kThinking),
      "https://other.example.com/v1"));
  EXPECT_EQ(elsewhere.FindList("messages")->front().GetDict(),
            base::test::ParseJsonDict(
                R"({"role": "user", "content": [
                      {"type": "text", "text": "Open example.com"}]})"));
  EXPECT_EQ((*elsewhere.FindList("messages"))[1]
                .GetDict()
                .FindList("content")
                ->front()
                .GetDict()
                .FindString("type")
                ->c_str(),
            std::string("tool_use"));

  std::string other_model =
      Chat(static_cast<int>(ProviderFormat::kAnthropicMessages), kThinking);
  other_model.replace(other_model.find("\"a-model\""), 9, "\"b-model\"");
  EXPECT_EQ(*(*Parsed(RequestIn(ProviderFormat::kAnthropicMessages, other_model,
                                kAddress))
                   .FindList("messages"))[1]
                 .GetDict()
                 .FindList("content")
                 ->front()
                 .GetDict()
                 .FindString("type"),
            "tool_use");

  const base::DictValue switched = Parsed(RequestIn(
      ProviderFormat::kOpenAiResponses,
      Chat(static_cast<int>(ProviderFormat::kAnthropicMessages), kThinking),
      kAddress));
  EXPECT_EQ(*(*switched.FindList("input"))[1].GetDict().FindString("type"),
            "function_call");

  // Chat completions gets the chat without them.
  const base::DictValue chat = Parsed(RequestIn(
      ProviderFormat::kChatCompletions,
      Chat(static_cast<int>(ProviderFormat::kOpenAiResponses), kReasoning),
      kAddress));
  EXPECT_FALSE((*chat.FindList("messages"))[2].GetDict().Find(kKeptPartsKey));
}

TEST(FormatsTest, KeptPartsStayInTheTurnThatMadeThem) {
  // Once the user writes again, earlier turns may have been shortened, so
  // what they kept stays behind.
  std::string chat =
      Chat(static_cast<int>(ProviderFormat::kAnthropicMessages), kThinking);
  chat.insert(chat.find("\n  ],\n  \"tools\""),
              R"(, {"role": "user", "content": "Thanks"})");
  const base::DictValue request =
      Parsed(RequestIn(ProviderFormat::kAnthropicMessages, chat, kAddress));
  EXPECT_EQ(*(*request.FindList("messages"))[1]
                 .GetDict()
                 .FindList("content")
                 ->front()
                 .GetDict()
                 .FindString("type"),
            "tool_use");
}

TEST(FormatsTest, AnAnthropicAnswerReadsAsChatCompletions) {
  const base::DictValue answer =
      Parsed(AnswerFrom(ProviderFormat::kAnthropicMessages, R"({
        "content": [
          {"type": "thinking", "thinking": "Open it.", "signature": "sig"},
          {"type": "text", "text": "Opening it."},
          {"type": "tool_use", "id": "toolu_1", "name": "open",
           "input": {"url": "https://example.com"}}],
        "stop_reason": "tool_use"})",
                        kAddress, kModel));

  EXPECT_EQ(answer, base::test::ParseJsonDict(R"({"choices": [{
    "finish_reason": "tool_calls",
    "message": {
      "role": "assistant",
      "content": "Opening it.",
      "tool_calls": [{"id": "toolu_1", "type": "function",
                      "function": {"name": "open",
                                   "arguments": "{\"url\":\"https://example.com\"}"}}],
      "uw_kept": {"address": "https://api.example.com/v1", "model": "a-model",
                  "format": 1,
                  "items": [{"type": "thinking", "thinking": "Open it.",
                             "signature": "sig"}]}}}]})"));
}

TEST(FormatsTest, AResponsesAnswerReadsAsChatCompletions) {
  const base::DictValue answer =
      Parsed(AnswerFrom(ProviderFormat::kOpenAiResponses, R"({
        "status": "completed",
        "output": [
          {"type": "reasoning", "id": "rs_1", "encrypted_content": "sealed"},
          {"type": "message", "role": "assistant",
           "content": [{"type": "output_text", "text": "Done."}]}]})",
                        kAddress, kModel));

  EXPECT_EQ(answer, base::test::ParseJsonDict(R"({"choices": [{
    "finish_reason": "stop",
    "message": {
      "role": "assistant",
      "content": "Done.",
      "uw_kept": {"address": "https://api.example.com/v1", "model": "a-model",
                  "format": 2,
                  "items": [{"type": "reasoning", "id": "rs_1",
                             "encrypted_content": "sealed"}]}}}]})"));
}

TEST(FormatsTest, AnAnswerCutShortSaysSo) {
  EXPECT_EQ(
      *Parsed(AnswerFrom(ProviderFormat::kAnthropicMessages,
                         R"({"content": [], "stop_reason": "max_tokens"})",
                         kAddress, kModel))
           .FindList("choices")
           ->front()
           .GetDict()
           .FindString("finish_reason"),
      "length");
  EXPECT_EQ(*Parsed(AnswerFrom(ProviderFormat::kOpenAiResponses,
                               R"({"status": "incomplete", "output": []})",
                               kAddress, kModel))
                 .FindList("choices")
                 ->front()
                 .GetDict()
                 .FindString("finish_reason"),
            "length");
}

TEST(FormatsTest, WhatIsNotARequestOrAnAnswerIsRefused) {
  EXPECT_FALSE(RequestIn(ProviderFormat::kAnthropicMessages, "[]", kAddress));
  EXPECT_FALSE(RequestIn(ProviderFormat::kOpenAiResponses, R"({"model": "a"})",
                         kAddress));
  EXPECT_FALSE(AnswerFrom(ProviderFormat::kAnthropicMessages,
                          R"({"error": {"message": "no"}})", kAddress, kModel));
  EXPECT_FALSE(AnswerFrom(ProviderFormat::kOpenAiResponses, "not json",
                          kAddress, kModel));
  // A chat-completions answer passes through as it is.
  EXPECT_EQ(AnswerFrom(ProviderFormat::kChatCompletions, "as it is", kAddress,
                       kModel),
            "as it is");
}

TEST(FormatsTest, AnEffortTakesEachFormatsOwnField) {
  constexpr char kAsk[] =
      R"({"model": "m", "messages": [{"role": "user", "content": "Go"}]})";
  // Gateways nest it; other chat-completions providers take it at the top.
  EXPECT_EQ(*Parsed(WithEffort(kAsk, ProviderPreset::kOpenRouter, "high"))
                 .FindStringByDottedPath("reasoning.effort"),
            "high");
  const std::string plain = WithEffort(kAsk, ProviderPreset::kCustom, "low");
  EXPECT_EQ(*Parsed(plain).FindString("reasoning_effort"), "low");
  EXPECT_EQ(WithEffort(kAsk, ProviderPreset::kCustom, ""), kAsk);

  // Anthropic thinks adaptively at that effort; OpenAI reasons at it.
  const base::DictValue anthropic =
      Parsed(RequestIn(ProviderFormat::kAnthropicMessages, plain, kAddress));
  EXPECT_EQ(*anthropic.FindStringByDottedPath("thinking.type"), "adaptive");
  EXPECT_EQ(*anthropic.FindStringByDottedPath("output_config.effort"), "low");
  EXPECT_FALSE(anthropic.Find("reasoning_effort"));
  const base::DictValue openai =
      Parsed(RequestIn(ProviderFormat::kOpenAiResponses, plain, kAddress));
  EXPECT_EQ(*openai.FindStringByDottedPath("reasoning.effort"), "low");
  EXPECT_EQ(
      *Parsed(RequestIn(ProviderFormat::kChatCompletions, plain, kAddress))
           .FindString("reasoning_effort"),
      "low");
}

TEST(FormatsTest, ALimitCarriesOver) {
  constexpr char kNaming[] = R"({"model": "m", "max_tokens": 4096,
    "messages": [{"role": "user", "content": "Name it"}]})";
  EXPECT_EQ(
      Parsed(RequestIn(ProviderFormat::kAnthropicMessages, kNaming, kAddress))
          .FindInt("max_tokens"),
      4096);
  EXPECT_EQ(
      Parsed(RequestIn(ProviderFormat::kOpenAiResponses, kNaming, kAddress))
          .FindInt("max_output_tokens"),
      4096);
}

// A request without tools is sent once, so only its instructions, which the
// next one repeats, are cached.
TEST(FormatsTest, AnthropicCachesAOneOffRequestsInstructionsOnly) {
  constexpr char kCheck[] = R"({"model": "m", "messages": [
    {"role": "system", "content": "Judge the command."},
    {"role": "user", "content": "ls"}]})";
  const base::DictValue request =
      Parsed(RequestIn(ProviderFormat::kAnthropicMessages, kCheck, kAddress));
  EXPECT_FALSE(request.Find("cache_control"));
  EXPECT_EQ(*request.FindList("system"), base::test::ParseJsonList(R"([
    {"type": "text", "text": "Judge the command.",
     "cache_control": {"type": "ephemeral", "ttl": "1h"}}])"));
}

// An image in a user message goes as each format's own image part. A message
// that shows recalled images is not a new turn, so what the model kept still
// goes back, and chat completions does not see uw's mark on it.
TEST(FormatsTest, AnImageGoesInEachFormatsOwnParts) {
  constexpr char kImages[] = R"({
    "model": "a-model",
    "messages": [
      {"role": "user", "content": [
        {"type": "text", "text": "Find this sofa"},
        {"type": "image_url",
         "image_url": {"url": "data:image/png;base64,iVBOR"}}]},
      {"role": "assistant", "content": "",
       "tool_calls": [{"id": "call_1", "type": "function",
                       "function": {"name": "recall",
                                    "arguments": "{\"image\":1}"}}],
       "uw_kept": {"address": "https://api.example.com/v1",
                   "model": "a-model", "format": 1,
                   "items": [{"type": "thinking", "thinking": "Look again.",
                              "signature": "sig"}]}},
      {"role": "tool", "tool_call_id": "call_1", "content": "{\"image\":1}"},
      {"role": "user", "uw_shown": true, "content": [
        {"type": "image_url",
         "image_url": {"url": "data:image/jpeg;base64,/9j/4"}}]}
    ]
  })";

  const base::DictValue anthropic =
      Parsed(RequestIn(ProviderFormat::kAnthropicMessages, kImages, kAddress));
  EXPECT_EQ(*anthropic.FindList("messages"), base::test::ParseJsonList(R"([
    {"role": "user", "content": [
      {"type": "text", "text": "Find this sofa"},
      {"type": "image", "source": {"type": "base64",
                                   "media_type": "image/png",
                                   "data": "iVBOR"}}]},
    {"role": "assistant", "content": [
      {"type": "thinking", "thinking": "Look again.", "signature": "sig"},
      {"type": "tool_use", "id": "call_1", "name": "recall",
       "input": {"image": 1}}]},
    {"role": "user", "content": [
      {"type": "tool_result", "tool_use_id": "call_1",
       "content": "{\"image\":1}"},
      {"type": "image", "source": {"type": "base64",
                                   "media_type": "image/jpeg",
                                   "data": "/9j/4"}}]}
  ])"));

  const base::DictValue responses =
      Parsed(RequestIn(ProviderFormat::kOpenAiResponses, kImages, kAddress));
  EXPECT_EQ((*responses.FindList("input"))[0], base::test::ParseJson(R"(
    {"role": "user", "content": [
      {"type": "input_text", "text": "Find this sofa"},
      {"type": "input_image", "image_url": "data:image/png;base64,iVBOR"}]}
  )"));

  const base::DictValue chat =
      Parsed(RequestIn(ProviderFormat::kChatCompletions, kImages, kAddress));
  const base::ListValue& messages = *chat.FindList("messages");
  EXPECT_FALSE(messages[3].GetDict().contains("uw_shown"));
  EXPECT_EQ(*messages[0].GetDict().FindList("content"),
            *base::test::ParseJsonDict(kImages)
                 .FindList("messages")
                 ->front()
                 .GetDict()
                 .FindList("content"));
}

}  // namespace
}  // namespace uw
