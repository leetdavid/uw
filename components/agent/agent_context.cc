// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/agent_context.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace uw {
namespace {

// A result this short costs less than the line that would replace it.
constexpr size_t kShortResult = 300;

std::string Json(const base::DictValue& value) {
  return base::WriteJson(value).value_or("{}");
}

}  // namespace

AgentContext::AgentContext() {
  messages_.Append(AgentInstructions());
}

AgentContext::~AgentContext() = default;

// static
std::unique_ptr<AgentContext> AgentContext::FromValue(
    const base::DictValue& saved) {
  const base::ListValue* messages = saved.FindList("messages");
  const base::ListValue* results = saved.FindList("results");
  if (!messages || messages->empty() || !results ||
      !std::ranges::all_of(*messages, &base::Value::is_dict)) {
    return nullptr;
  }
  auto context = std::make_unique<AgentContext>();
  // The instructions of this version of uw, not the saved ones.
  base::ListValue kept = messages->Clone();
  kept[0] = base::Value(AgentInstructions());
  context->messages_ = std::move(kept);
  for (const base::Value& entry : *results) {
    const base::DictValue* result = entry.GetIfDict();
    const std::string* tool = result ? result->FindString("tool") : nullptr;
    const base::DictValue* args = result ? result->FindDict("args") : nullptr;
    const base::DictValue* value =
        result ? result->FindDict("result") : nullptr;
    const std::optional<int> message =
        result ? result->FindInt("message") : std::nullopt;
    if (!tool || !args || !value || !message || *message < 1 ||
        static_cast<size_t>(*message) >= messages->size()) {
      return nullptr;
    }
    context->results_.push_back(
        {static_cast<int>(context->results_.size()) + 1, *tool, args->Clone(),
         value->Clone(), static_cast<size_t>(*message),
         result->FindBool("shortened").value_or(false)});
  }
  return context;
}

base::DictValue AgentContext::ToValue() const {
  base::ListValue results;
  for (const Result& result : results_) {
    results.Append(base::DictValue()
                       .Set("tool", result.tool)
                       .Set("args", result.args.Clone())
                       .Set("result", result.result.Clone())
                       .Set("message", static_cast<int>(result.message))
                       .Set("shortened", result.shortened));
  }
  return base::DictValue()
      .Set("messages", messages_.Clone())
      .Set("results", std::move(results));
}

void AgentContext::AddUser(std::string_view text) {
  messages_.Append(UserMessage(text));
}

void AgentContext::AddAssistant(const ModelTurn& turn) {
  messages_.Append(AssistantMessage(turn));
}

int AgentContext::AddResult(std::string_view call_id,
                            std::string_view tool,
                            const base::DictValue& args,
                            const base::DictValue& result) {
  const int step = static_cast<int>(results_.size()) + 1;
  const std::optional<int> tab = args.FindInt("tab");
  if (tool == "read" && tab && !result.contains("error")) {
    for (Result& older : results_) {
      if (older.tool == "read" && older.args.FindInt("tab") == tab &&
          !older.shortened) {
        Shorten(older);
      }
    }
  }
  messages_.Append(ToolMessage(call_id, result.Clone().Set("step", step)));
  results_.push_back({step, std::string(tool), args.Clone(), result.Clone(),
                      messages_.size() - 1});
  return step;
}

base::DictValue AgentContext::Recall(const base::DictValue& args) const {
  const std::optional<int> step = args.FindInt("step");
  if (!step || *step < 1 || static_cast<size_t>(*step) > results_.size()) {
    return base::DictValue().Set(
        "error",
        base::StrCat({"There is no step ",
                      step ? base::NumberToString(*step) : "like that", "."}));
  }
  const Result& result = results_[*step - 1];
  base::DictValue recalled = base::DictValue()
                                 .Set("step", result.step)
                                 .Set("tool", result.tool)
                                 .Set("args", result.args.Clone())
                                 .Set("result", result.result.Clone());
  if (result.tool == "read") {
    recalled.Set("note", base::StrCat({"The tab as it was at step ",
                                       base::NumberToString(result.step),
                                       ". Its element numbers cannot be "
                                       "clicked; read the tab again to act."}));
  }
  return recalled;
}

void AgentContext::ShortenAll() {
  for (Result& result : results_) {
    if (!result.shortened && CanShorten(result)) {
      Shorten(result);
    }
  }
}

void AgentContext::SetSkills(std::string_view skills) {
  base::DictValue instructions = AgentInstructions();
  if (!skills.empty()) {
    std::string content = *instructions.FindString("content");
    base::StrAppend(&content, {"\n\nSkills:\n", skills});
    instructions.Set("content", std::move(content));
  }
  messages_[0] = base::Value(std::move(instructions));
}

void AgentContext::CloseOpenCalls(std::string_view error) {
  size_t turn = messages_.size();
  while (turn > 0) {
    const std::string* role = messages_[turn - 1].GetDict().FindString("role");
    if (role && *role == "assistant") {
      break;
    }
    if (!role || *role != "tool") {
      return;
    }
    --turn;
  }
  const base::ListValue* calls =
      turn == 0 ? nullptr
                : messages_[turn - 1].GetDict().FindList("tool_calls");
  if (!calls) {
    return;
  }
  const base::ListValue open = calls->Clone();
  for (const base::Value& entry : open) {
    const base::DictValue* call = entry.GetIfDict();
    const std::string* id = call ? call->FindString("id") : nullptr;
    const std::string* name =
        call ? call->FindStringByDottedPath("function.name") : nullptr;
    const std::string* arguments =
        call ? call->FindStringByDottedPath("function.arguments") : nullptr;
    if (!id || !name) {
      continue;
    }
    const bool answered = std::any_of(
        messages_.begin() + turn, messages_.end(), [&](const base::Value& m) {
          const std::string* answers = m.GetDict().FindString("tool_call_id");
          return answers && *answers == *id;
        });
    if (!answered) {
      AddResult(*id, *name,
                base::JSONReader::ReadDict(arguments ? *arguments : "",
                                           base::JSON_PARSE_RFC)
                    .value_or(base::DictValue()),
                base::DictValue().Set("error", error));
    }
  }
}

std::optional<std::string> AgentContext::Request(std::string_view model,
                                                 size_t max_length) {
  for (;;) {
    std::optional<std::string> body =
        BuildAgentRequestBody(model, messages_, AgentToolSchemas());
    if (!body || body->size() <= max_length) {
      return body;
    }
    auto oldest = std::ranges::find_if(results_, [](const Result& result) {
      return !result.shortened && CanShorten(result);
    });
    if (oldest == results_.end()) {
      return body;
    }
    Shorten(*oldest);
  }
}

// static
bool AgentContext::CanShorten(const Result& result) {
  if (result.result.contains("error")) {
    return false;
  }
  return result.tool == "read" || result.tool == "recall" ||
         Json(result.result).size() > kShortResult;
}

void AgentContext::Shorten(Result& result) {
  const std::string step = base::NumberToString(result.step);
  std::string line;
  if (result.tool == "read") {
    const std::string* title = result.result.FindString("title");
    const std::string* url = result.result.FindString("url");
    const std::string* text = result.result.FindString("text");
    line = base::StrCat(
        {"Step ", step, " read tab ",
         base::NumberToString(result.args.FindInt("tab").value_or(0)), ", \"",
         title ? *title : "", "\" at ", url ? *url : "", ": ",
         base::NumberToString(text ? text->size() : 0), " characters"});
  } else if (result.tool == "recall") {
    line = base::StrCat(
        {"Step ", step, " recalled step ",
         base::NumberToString(result.args.FindInt("step").value_or(0))});
  } else {
    line = base::StrCat({"Step ", step, " used ", result.tool, ": ",
                         base::NumberToString(Json(result.result).size()),
                         " characters"});
  }
  base::StrAppend(&line, {", shortened. Call recall with step ", step,
                          " to see it again."});
  messages_[result.message].GetDict().Set(
      "content", Json(base::DictValue()
                          .Set("step", result.step)
                          .Set("shortened", std::move(line))));
  result.shortened = true;
}

}  // namespace uw
