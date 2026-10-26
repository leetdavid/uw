// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/mcp.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/version_info/version_info.h"

namespace uw::mcp {
namespace {

// Errors the MCP specification itself defines, which only modern servers
// send.
constexpr int kHeaderMismatch = -32020;
constexpr int kUnsupportedProtocolVersion = -32022;
constexpr int kMethodNotFound = -32601;

base::DictValue ClientInfo() {
  return base::DictValue()
      .Set("name", "uw")
      .Set("version", version_info::GetVersionNumber());
}

std::string Write(const base::DictValue& message) {
  return base::WriteJson(message).value_or("");
}

void AppendText(std::string& text, std::string_view more) {
  if (more.empty()) {
    return;
  }
  base::StrAppend(&text, {text.empty() ? "" : "\n\n", more});
}

// One content block of a tool result or a prompt message.
void ReadBlock(const base::DictValue& block, Content& content) {
  const std::string* type = block.FindString("type");
  if (!type) {
    return;
  }
  if (*type == "text") {
    if (const std::string* text = block.FindString("text")) {
      AppendText(content.text, *text);
    }
  } else if (*type == "image") {
    const std::string* data = block.FindString("data");
    const std::string* mime = block.FindString("mimeType");
    if (data && mime && base::StartsWith(*mime, "image/")) {
      content.images.push_back(
          base::StrCat({"data:", *mime, ";base64,", *data}));
    }
  } else if (*type == "resource_link") {
    const std::string* uri = block.FindString("uri");
    const std::string* name = block.FindString("name");
    if (uri) {
      AppendText(content.text, base::StrCat({"Link: ", name ? *name : *uri,
                                             " <", *uri, ">"}));
    }
  } else if (*type == "resource") {
    const base::DictValue* resource = block.FindDict("resource");
    const std::string* text = resource ? resource->FindString("text") : nullptr;
    const std::string* uri = resource ? resource->FindString("uri") : nullptr;
    if (text) {
      AppendText(content.text,
                 uri ? base::StrCat({*uri, ":\n", *text}) : *text);
    } else if (uri) {
      AppendText(content.text,
                 base::StrCat({"[A file uw can't show: ", *uri, "]"}));
    }
  } else if (*type == "audio") {
    AppendText(content.text, "[Audio left out: uw can't play it to the model]");
  }
}

}  // namespace

Error::Error() = default;
Error::Error(const Error&) = default;
Error& Error::operator=(const Error&) = default;
Error::~Error() = default;

Message::Message() = default;
Message::Message(Message&&) = default;
Message& Message::operator=(Message&&) = default;
Message::~Message() = default;

Tool::Tool() = default;
Tool::Tool(const Tool& other)
    : name(other.name),
      description(other.description),
      input_schema(other.input_schema.Clone()),
      read_only(other.read_only) {}
Tool& Tool::operator=(const Tool& other) {
  name = other.name;
  description = other.description;
  input_schema = other.input_schema.Clone();
  read_only = other.read_only;
  return *this;
}
Tool::Tool(Tool&&) = default;
Tool& Tool::operator=(Tool&&) = default;
Tool::~Tool() = default;

Prompt::Prompt() = default;
Prompt::Prompt(const Prompt&) = default;
Prompt& Prompt::operator=(const Prompt&) = default;
Prompt::~Prompt() = default;

Content::Content() = default;
Content::Content(Content&&) = default;
Content& Content::operator=(Content&&) = default;
Content::~Content() = default;

std::string RequestBody(int id,
                        Era era,
                        std::string_view method,
                        base::DictValue params) {
  if (era == Era::kModern) {
    params.Set(
        "_meta",
        base::DictValue()
            .Set("io.modelcontextprotocol/protocolVersion", kModernVersion)
            .Set("io.modelcontextprotocol/clientInfo", ClientInfo())
            .Set("io.modelcontextprotocol/clientCapabilities",
                 base::DictValue()));
  }
  return Write(base::DictValue()
                   .Set("jsonrpc", "2.0")
                   .Set("id", id)
                   .Set("method", method)
                   .Set("params", std::move(params)));
}

std::string AnswerToServer(const base::Value& id, std::string_view method) {
  base::DictValue answer;
  answer.Set("jsonrpc", "2.0");
  answer.Set("id", id.Clone());
  if (method == "ping") {
    answer.Set("result", base::DictValue());
  } else {
    answer.Set("error", base::DictValue()
                            .Set("code", kMethodNotFound)
                            .Set("message", "uw does not offer this."));
  }
  return Write(answer);
}

base::DictValue InitializeParams() {
  return base::DictValue()
      .Set("protocolVersion", kLegacyVersion)
      .Set("capabilities", base::DictValue())
      .Set("clientInfo", ClientInfo());
}

std::string InitializedNotification() {
  return Write(base::DictValue()
                   .Set("jsonrpc", "2.0")
                   .Set("method", "notifications/initialized"));
}

std::optional<Message> ParseMessage(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  Message message;
  if (const std::string* method = dict->FindString("method")) {
    message.method = *method;
    if (const base::Value* id = dict->Find("id")) {
      message.request_id = id->Clone();
    }
    return message;
  }
  message.id = dict->FindInt("id");
  if (!message.id) {
    // A response uw cannot match, such as an error to a message it could not
    // read.
    if (!dict->FindDict("error")) {
      return std::nullopt;
    }
  }
  if (base::DictValue* result = dict->FindDict("result")) {
    message.result = std::move(*result);
    return message;
  }
  const base::DictValue* error = dict->FindDict("error");
  if (!error) {
    return std::nullopt;
  }
  Error parsed;
  parsed.code = error->FindInt("code").value_or(0);
  if (const std::string* text = error->FindString("message")) {
    parsed.message = *text;
  }
  if (const base::DictValue* data = error->FindDict("data")) {
    if (const base::ListValue* supported = data->FindList("supported")) {
      for (const base::Value& version : *supported) {
        if (version.is_string()) {
          parsed.supported.push_back(version.GetString());
        }
      }
    }
  }
  message.error = std::move(parsed);
  return message;
}

std::vector<Message> ParseEventStream(std::string_view body) {
  std::vector<Message> messages;
  std::string data;
  const auto flush = [&] {
    if (std::optional<Message> message = ParseMessage(data)) {
      messages.push_back(std::move(*message));
    }
    data.clear();
  };
  for (std::string_view line : base::SplitStringPiece(
           body, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    line = base::TrimString(line, "\r", base::TRIM_TRAILING);
    if (line.empty()) {
      flush();
    } else if (base::StartsWith(line, "data:")) {
      line.remove_prefix(5);
      if (base::StartsWith(line, " ")) {
        line.remove_prefix(1);
      }
      base::StrAppend(&data, {data.empty() ? "" : "\n", line});
    }
  }
  flush();
  return messages;
}

bool IsModernError(const Error& error) {
  return error.code <= kHeaderMismatch &&
         error.code >= kUnsupportedProtocolVersion;
}

std::vector<Tool> ParseTools(const base::DictValue& result) {
  std::vector<Tool> tools;
  const base::ListValue* list = result.FindList("tools");
  if (!list) {
    return tools;
  }
  for (const base::Value& item : *list) {
    if (std::optional<Tool> tool =
            item.is_dict() ? ToolFromValue(item.GetDict()) : std::nullopt) {
      tools.push_back(std::move(*tool));
    }
  }
  return tools;
}

std::vector<Prompt> ParsePrompts(const base::DictValue& result) {
  std::vector<Prompt> prompts;
  const base::ListValue* list = result.FindList("prompts");
  if (!list) {
    return prompts;
  }
  for (const base::Value& item : *list) {
    if (std::optional<Prompt> prompt =
            item.is_dict() ? PromptFromValue(item.GetDict()) : std::nullopt) {
      prompts.push_back(std::move(*prompt));
    }
  }
  return prompts;
}

std::optional<std::string> NextCursor(const base::DictValue& result) {
  const std::string* cursor = result.FindString("nextCursor");
  if (!cursor || cursor->empty()) {
    return std::nullopt;
  }
  return *cursor;
}

base::DictValue ToolToValue(const Tool& tool) {
  return base::DictValue()
      .Set("name", tool.name)
      .Set("description", tool.description)
      .Set("inputSchema", tool.input_schema.Clone())
      .Set("annotations",
           base::DictValue().Set("readOnlyHint", tool.read_only));
}

std::optional<Tool> ToolFromValue(const base::DictValue& value) {
  const std::string* name = value.FindString("name");
  if (!name || name->empty()) {
    return std::nullopt;
  }
  Tool tool;
  tool.name = *name;
  if (const std::string* description = value.FindString("description")) {
    tool.description = *description;
  }
  if (const base::DictValue* schema = value.FindDict("inputSchema")) {
    tool.input_schema = schema->Clone();
  } else {
    tool.input_schema.Set("type", "object");
  }
  if (const base::DictValue* annotations = value.FindDict("annotations")) {
    tool.read_only = annotations->FindBool("readOnlyHint").value_or(false);
  }
  return tool;
}

base::DictValue PromptToValue(const Prompt& prompt) {
  base::ListValue arguments;
  for (const Prompt::Argument& argument : prompt.arguments) {
    arguments.Append(base::DictValue()
                         .Set("name", argument.name)
                         .Set("description", argument.description)
                         .Set("required", argument.required));
  }
  return base::DictValue()
      .Set("name", prompt.name)
      .Set("description", prompt.description)
      .Set("arguments", std::move(arguments));
}

std::optional<Prompt> PromptFromValue(const base::DictValue& value) {
  const std::string* name = value.FindString("name");
  if (!name || name->empty()) {
    return std::nullopt;
  }
  Prompt prompt;
  prompt.name = *name;
  if (const std::string* description = value.FindString("description")) {
    prompt.description = *description;
  }
  if (const base::ListValue* arguments = value.FindList("arguments")) {
    for (const base::Value& item : *arguments) {
      const base::DictValue* argument = item.GetIfDict();
      const std::string* argument_name =
          argument ? argument->FindString("name") : nullptr;
      if (!argument_name || argument_name->empty()) {
        continue;
      }
      Prompt::Argument parsed;
      parsed.name = *argument_name;
      if (const std::string* description =
              argument->FindString("description")) {
        parsed.description = *description;
      }
      parsed.required = argument->FindBool("required").value_or(false);
      prompt.arguments.push_back(std::move(parsed));
    }
  }
  return prompt;
}

Content ParseToolResult(const base::DictValue& result) {
  Content content;
  const std::string* type = result.FindString("resultType");
  if (type && *type == "input_required") {
    content.text = "The tool asked for more input, which uw can't give yet.";
    content.failed = true;
    return content;
  }
  if (const base::ListValue* blocks = result.FindList("content")) {
    for (const base::Value& block : *blocks) {
      if (block.is_dict()) {
        ReadBlock(block.GetDict(), content);
      }
    }
  }
  if (content.text.empty()) {
    if (const base::Value* structured = result.Find("structuredContent")) {
      content.text = base::WriteJson(*structured).value_or("");
    }
  }
  content.failed = result.FindBool("isError").value_or(false);
  return content;
}

Content ParsePromptResult(const base::DictValue& result) {
  Content content;
  const base::ListValue* messages = result.FindList("messages");
  if (!messages) {
    return content;
  }
  for (const base::Value& item : *messages) {
    const base::DictValue* message = item.GetIfDict();
    const base::DictValue* block =
        message ? message->FindDict("content") : nullptr;
    if (block) {
      ReadBlock(*block, content);
    }
  }
  return content;
}

}  // namespace uw::mcp
