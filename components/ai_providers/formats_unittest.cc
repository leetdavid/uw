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

// A chat in chat completions' shape: instructions, a question, a tool call
// with what the provider kept, its result, and the answer.
constexpr char kChat[] = R"({
  "model": "a-model",
  "messages": [
    {"role": "system", "content": "Be brief."},
    {"role": "user", "content": "Open example.com"},
    {"role": "assistant", "content": "",
     "tool_calls": [{"id": "call_1", "type": "function",
                     "function": {"name": "open",
                                  "arguments": "{\"url\":\"https://example.com\"}"}}],
     "uw_kept": {"address": "https://api.example.com/v1", "format": FORMAT,
                 "items": [KEPT]}},
    {"role": "tool", "tool_call_id": "call_1", "content": "{\"ok\":true}"},
    {"role": "user", "content": "Thanks"}
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
    "max_tokens": 8192,
    "system": "Be brief.",
    "messages": [
      {"role": "user", "content": [{"type": "text", "text": "Open example.com"}]},
      {"role": "assistant", "content": [
        {"type": "thinking", "thinking": "Open it.", "signature": "sig"},
        {"type": "tool_use", "id": "call_1", "name": "open",
         "input": {"url": "https://example.com"}}]},
      {"role": "user", "content": [
        {"type": "tool_result", "tool_use_id": "call_1", "content": "{\"ok\":true}"},
        {"type": "text", "text": "Thanks"}]}
    ],
    "tools": [{"name": "open", "description": "Opens a page.",
               "input_schema": {"type": "object"}}]
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
       "output": "{\"ok\":true}"},
      {"role": "user", "content": "Thanks"}
    ],
    "tools": [{"type": "function", "name": "open",
               "description": "Opens a page.",
               "parameters": {"type": "object"}}]
  })"));
}

TEST(FormatsTest, KeptPartsGoOnlyBackToWhereTheyCameFrom) {
  // Another provider, or another format, never sees them.
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

TEST(FormatsTest, AnAnthropicAnswerReadsAsChatCompletions) {
  const base::DictValue answer =
      Parsed(AnswerFrom(ProviderFormat::kAnthropicMessages, R"({
        "content": [
          {"type": "thinking", "thinking": "Open it.", "signature": "sig"},
          {"type": "text", "text": "Opening it."},
          {"type": "tool_use", "id": "toolu_1", "name": "open",
           "input": {"url": "https://example.com"}}],
        "stop_reason": "tool_use"})",
                        kAddress));

  EXPECT_EQ(answer, base::test::ParseJsonDict(R"({"choices": [{
    "finish_reason": "tool_calls",
    "message": {
      "role": "assistant",
      "content": "Opening it.",
      "tool_calls": [{"id": "toolu_1", "type": "function",
                      "function": {"name": "open",
                                   "arguments": "{\"url\":\"https://example.com\"}"}}],
      "uw_kept": {"address": "https://api.example.com/v1", "format": 1,
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
                        kAddress));

  EXPECT_EQ(answer, base::test::ParseJsonDict(R"({"choices": [{
    "finish_reason": "stop",
    "message": {
      "role": "assistant",
      "content": "Done.",
      "uw_kept": {"address": "https://api.example.com/v1", "format": 2,
                  "items": [{"type": "reasoning", "id": "rs_1",
                             "encrypted_content": "sealed"}]}}}]})"));
}

TEST(FormatsTest, AnAnswerCutShortSaysSo) {
  EXPECT_EQ(
      *Parsed(AnswerFrom(ProviderFormat::kAnthropicMessages,
                         R"({"content": [], "stop_reason": "max_tokens"})",
                         kAddress))
           .FindList("choices")
           ->front()
           .GetDict()
           .FindString("finish_reason"),
      "length");
  EXPECT_EQ(
      *Parsed(AnswerFrom(ProviderFormat::kOpenAiResponses,
                         R"({"status": "incomplete", "output": []})", kAddress))
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
                          R"({"error": {"message": "no"}})", kAddress));
  EXPECT_FALSE(
      AnswerFrom(ProviderFormat::kOpenAiResponses, "not json", kAddress));
  // A chat-completions answer passes through as it is.
  EXPECT_EQ(AnswerFrom(ProviderFormat::kChatCompletions, "as it is", kAddress),
            "as it is");
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

}  // namespace
}  // namespace uw
