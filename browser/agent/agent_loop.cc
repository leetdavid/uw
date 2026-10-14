// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace uw {
namespace {

// A chat that takes this many turns without an answer has lost its way.
constexpr int kMaxTurns = 40;
// A large model can think for a while before it answers.
constexpr base::TimeDelta kModelTimeout = base::Minutes(2);

AgentLoop::Step Failed(std::string text) {
  AgentLoop::Step step;
  step.kind = AgentLoop::Step::Kind::kFailed;
  step.text = std::move(text);
  return step;
}

}  // namespace

AgentLoop::AgentLoop(tabs::TabInterface& chat, AgentModel model, OnStep on_step)
    : tools_(chat),
      model_(std::move(model)),
      on_step_(std::move(on_step)),
      loader_(chat.GetBrowserWindowInterface()
                  ->GetProfile()
                  ->GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess()) {
  messages_.Append(AgentInstructions());
}

AgentLoop::~AgentLoop() = default;

void AgentLoop::Send(std::string_view text) {
  if (working_) {
    return;
  }
  working_ = true;
  turns_ = 0;
  messages_.Append(UserMessage(text));
  AskModel();
}

void AgentLoop::AskModel() {
  if (++turns_ > kMaxTurns) {
    Finish(Failed(base::StrCat({"The agent stopped after ",
                                base::NumberToString(kMaxTurns),
                                " steps without an answer."})));
    return;
  }
  const std::optional<std::string> body =
      BuildAgentRequestBody(model_.model, messages_, AgentTools::Schemas());
  if (!body || !model_.url.is_valid()) {
    Finish(Failed("No agent model is set."));
    return;
  }
  client_ = std::make_unique<RemoteModelClient>(
      loader_, model_.url, model_.api_key, *body, kModelTimeout);
  client_->Send(
      base::BindOnce(&AgentLoop::OnModel, weak_factory_.GetWeakPtr()));
}

void AgentLoop::OnModel(RemoteModelClient::Outcome outcome) {
  if (!outcome.ok) {
    Finish(Failed(base::StrCat(
        {"The model request failed: ",
         outcome.http_status ? base::StrCat({"HTTP ", base::NumberToString(
                                                          outcome.http_status)})
                             : net::ErrorToShortString(outcome.net_error)})));
    return;
  }
  std::optional<ModelTurn> turn = ParseAgentResponse(outcome.body);
  if (!turn) {
    Finish(Failed("The model's answer could not be read."));
    return;
  }
  messages_.Append(AssistantMessage(*turn));
  if (turn->calls.empty()) {
    Step answer;
    answer.kind = Step::Kind::kAnswer;
    answer.text = turn->text;
    Finish(std::move(answer));
    return;
  }
  RunCall(std::move(*turn), 0);
}

void AgentLoop::RunCall(ModelTurn turn, size_t index) {
  if (index == turn.calls.size()) {
    // The model client that called OnModel may still be on the stack.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AgentLoop::AskModel, weak_factory_.GetWeakPtr()));
    return;
  }
  const ToolCall& call = turn.calls[index];
  std::optional<base::DictValue> args =
      base::JSONReader::ReadDict(call.arguments, base::JSON_PARSE_RFC);
  if (!args) {
    OnTool(std::move(turn), index, base::DictValue(),
           base::DictValue().Set("error", "The arguments were not JSON."));
    return;
  }
  const std::string name = call.name;
  base::DictValue asked = args->Clone();
  tools_.Call(name, *args,
              base::BindOnce(&AgentLoop::OnTool, weak_factory_.GetWeakPtr(),
                             std::move(turn), index, std::move(asked)));
}

void AgentLoop::OnTool(ModelTurn turn,
                       size_t index,
                       base::DictValue args,
                       base::DictValue result) {
  const ToolCall& call = turn.calls[index];
  messages_.Append(ToolMessage(call.id, result));
  Step step;
  step.kind = Step::Kind::kTool;
  step.tool = call.name;
  step.args = std::move(args);
  step.result = std::move(result);
  on_step_.Run(step);
  RunCall(std::move(turn), index + 1);
}

void AgentLoop::Finish(Step step) {
  working_ = false;
  on_step_.Run(step);
}

}  // namespace uw
