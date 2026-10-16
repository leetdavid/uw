// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/agent_context.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "uw/components/ai_providers/formats.h"

namespace uw {
namespace {

// A result this short costs less than the line that would replace it.
constexpr size_t kShortResult = 300;

std::string Json(const base::DictValue& value) {
  return base::WriteJson(value).value_or("{}");
}

base::DictValue TextPart(std::string_view text) {
  return base::DictValue().Set("type", "text").Set("text", text);
}

base::DictValue ImagePart(std::string_view url) {
  return base::DictValue()
      .Set("type", "image_url")
      .Set("image_url", base::DictValue().Set("url", url));
}

// Comes before each image, so the model knows its number.
std::string ImageLabel(int number) {
  return base::StrCat({"Image ", base::NumberToString(number), ":"});
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
  if (const base::ListValue* images = saved.FindList("images")) {
    for (const base::Value& entry : *images) {
      const base::DictValue* image = entry.GetIfDict();
      const std::optional<int> number =
          image ? image->FindInt("number") : std::nullopt;
      const std::string* url = image ? image->FindString("url") : nullptr;
      const std::optional<int> message =
          image ? image->FindInt("message") : std::nullopt;
      const std::optional<int> part =
          image ? image->FindInt("part") : std::nullopt;
      const base::ListValue* parts =
          message && *message > 0 &&
                  static_cast<size_t>(*message) < messages->size()
              ? (*messages)[*message].GetDict().FindList("content")
              : nullptr;
      if (!number || !url || !parts || !part || *part < 1 ||
          static_cast<size_t>(*part) >= parts->size()) {
        return nullptr;
      }
      context->images_.push_back(
          {*number, *url, static_cast<size_t>(*message),
           static_cast<size_t>(*part),
           image->FindBool("shortened").value_or(false)});
    }
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
  base::ListValue images;
  for (const Image& image : images_) {
    images.Append(base::DictValue()
                      .Set("number", image.number)
                      .Set("url", image.url)
                      .Set("message", static_cast<int>(image.message))
                      .Set("part", static_cast<int>(image.part))
                      .Set("shortened", image.shortened));
  }
  return base::DictValue()
      .Set("messages", messages_.Clone())
      .Set("results", std::move(results))
      .Set("images", std::move(images));
}

void AgentContext::AddUser(std::string_view text) {
  messages_.Append(UserMessage(text));
}

void AgentContext::AddUser(std::string_view text,
                           const std::vector<std::string>& images) {
  if (images.empty()) {
    AddUser(text);
    return;
  }
  int number = 0;
  for (const Image& image : images_) {
    number = std::max(number, image.number);
  }
  base::ListValue parts;
  parts.Append(TextPart(text));
  for (const std::string& url : images) {
    parts.Append(TextPart(ImageLabel(++number)));
    images_.push_back({number, url, messages_.size(), parts.size()});
    parts.Append(ImagePart(url));
  }
  messages_.Append(
      base::DictValue().Set("role", "user").Set("content", std::move(parts)));
}

void AgentContext::ShowImages(const std::vector<int>& numbers) {
  base::ListValue parts;
  parts.Append(TextPart("The images you recalled."));
  std::vector<Image> shown;
  for (int number : numbers) {
    if (const Image* image = FindImage(number)) {
      parts.Append(TextPart(ImageLabel(number)));
      shown.push_back({number, image->url, messages_.size(), parts.size()});
      parts.Append(ImagePart(image->url));
    }
  }
  if (shown.empty()) {
    return;
  }
  messages_.Append(base::DictValue()
                       .Set("role", "user")
                       .Set("content", std::move(parts))
                       .Set(kShownKey, true));
  std::ranges::move(shown, std::back_inserter(images_));
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
  if (const std::optional<int> image = args.FindInt("image")) {
    if (!FindImage(*image)) {
      return base::DictValue().Set(
          "error", base::StrCat({"There is no image ",
                                 base::NumberToString(*image), "."}));
    }
    return base::DictValue()
        .Set("image", *image)
        .Set("shown", "The image follows in the next message.");
  }
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
  for (Image& image : images_) {
    if (!image.shortened) {
      Shorten(image);
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
  // Images are sent as they are; only text counts against the limit.
  size_t pictures = 0;
  for (const Image& image : images_) {
    pictures += image.shortened ? 0 : image.url.size();
  }
  for (;;) {
    std::optional<std::string> body =
        BuildAgentRequestBody(model, messages_, AgentToolSchemas());
    if (!body || body->size() <= max_length + pictures) {
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
    const std::optional<int> image = result.args.FindInt("image");
    line = base::StrCat(
        {"Step ", step, " recalled ", image ? "image " : "step ",
         base::NumberToString(
             image ? *image : result.args.FindInt("step").value_or(0))});
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

void AgentContext::Shorten(Image& image) {
  base::ListValue* parts =
      messages_[image.message].GetDict().FindList("content");
  if (parts && image.part < parts->size()) {
    (*parts)[image.part] = base::Value(TextPart(base::StrCat(
        {"[Shrunk. Call recall with image ", base::NumberToString(image.number),
         " to see it again.]"})));
  }
  image.shortened = true;
}

const AgentContext::Image* AgentContext::FindImage(int number) const {
  const auto image = std::ranges::find(images_, number, &Image::number);
  return image == images_.end() ? nullptr : &*image;
}

}  // namespace uw
