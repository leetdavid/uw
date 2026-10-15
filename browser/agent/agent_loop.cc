// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
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
// Long enough for several full readings, short enough for most models.
constexpr size_t kMaxRequestLength = 200000;

// A file of a skill larger than this is not read.
constexpr size_t kMaxSkillFile = 256 * 1024;

base::DictValue Error(std::string_view message) {
  return base::DictValue().Set("error", message);
}

// What the skill tool answers: a skill's instructions, or with `file`, that
// file of the skill's folder. Blocks.
base::DictValue ReadSkill(base::FilePath path,
                          std::string name,
                          std::optional<std::string> file) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxSkillFile) ||
      !base::IsStringUTF8(text)) {
    return Error("That file of the skill cannot be read as text.");
  }
  if (file) {
    return base::DictValue()
        .Set("skill", name)
        .Set("file", *file)
        .Set("text", text);
  }
  return base::DictValue()
      .Set("skill", name)
      .Set("instructions", SkillInstructions(text));
}

AgentLoop::Step Failed(std::string text) {
  AgentLoop::Step step;
  step.kind = AgentLoop::Step::Kind::kFailed;
  step.text = std::move(text);
  return step;
}

}  // namespace

AgentLoop::AgentLoop(tabs::TabInterface& chat,
                     AgentModel model,
                     OnStep on_step,
                     std::unique_ptr<AgentContext> context,
                     base::RepeatingClosure on_change)
    : tools_(chat),
      model_(std::move(model)),
      on_step_(std::move(on_step)),
      loader_(chat.GetBrowserWindowInterface()
                  ->GetProfile()
                  ->GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess()),
      context_(context ? std::move(context) : std::make_unique<AgentContext>()),
      on_change_(std::move(on_change)) {}

AgentLoop::~AgentLoop() = default;

void AgentLoop::SetSkills(const std::vector<Skill>& skills) {
  skills_.clear();
  std::string list;
  for (const Skill& skill : skills) {
    skills_[skill.name] = skill.file;
    base::StrAppend(&list, {list.empty() ? "" : "\n", "- ", skill.name, ": ",
                            skill.description});
  }
  context_->SetSkills(list);
}

void AgentLoop::Send(std::string_view text) {
  if (working_) {
    return;
  }
  working_ = true;
  turns_ = 0;
  context_->AddUser(text);
  AskModel();
}

void AgentLoop::Resume() {
  Send(
      "uw quit while you were working and has started again. Your tabs may "
      "have reloaded or closed. Check where things stand before you go on, "
      "and do not repeat a step that may already have happened.");
}

void AgentLoop::AskModel() {
  if (++turns_ > kMaxTurns) {
    Finish(Failed(base::StrCat({"The agent stopped after ",
                                base::NumberToString(kMaxTurns),
                                " steps without an answer."})));
    return;
  }
  const std::optional<std::string> body =
      context_->Request(model_.model, kMaxRequestLength);
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
    std::string text = base::StrCat(
        {"The model request failed: ",
         outcome.http_status ? base::StrCat({"HTTP ", base::NumberToString(
                                                          outcome.http_status)})
                             : net::ErrorToShortString(outcome.net_error)});
    if (const std::string message = ModelErrorMessage(outcome.body);
        !message.empty()) {
      base::StrAppend(&text, {": ", message});
    }
    Finish(Failed(std::move(text)));
    return;
  }
  std::optional<ModelTurn> turn = ParseAgentResponse(outcome.body);
  if (!turn) {
    Finish(Failed("The model's answer could not be read."));
    return;
  }
  context_->AddAssistant(*turn);
  on_change_.Run();
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
  if (call.name == "recall") {
    base::DictValue recalled = context_->Recall(*args);
    OnTool(std::move(turn), index, std::move(*args), std::move(recalled));
    return;
  }
  if (call.name == "skill") {
    LoadSkill(std::move(turn), index, std::move(*args));
    return;
  }
  if (call.name == "save_skill") {
    if (!save_skill_) {
      OnTool(std::move(turn), index, std::move(*args),
             Error("Skills cannot be saved here."));
      return;
    }
    base::DictValue asked = args->Clone();
    save_skill_.Run(
        std::move(*args),
        base::BindOnce(&AgentLoop::OnTool, weak_factory_.GetWeakPtr(),
                       std::move(turn), index, std::move(asked)));
    return;
  }
  const std::string name = call.name;
  base::DictValue asked = args->Clone();
  tools_.Call(name, *args,
              base::BindOnce(&AgentLoop::OnTool, weak_factory_.GetWeakPtr(),
                             std::move(turn), index, std::move(asked)));
}

void AgentLoop::LoadSkill(ModelTurn turn, size_t index, base::DictValue args) {
  const std::string* name = args.FindString("name");
  const auto skill = name ? skills_.find(*name) : skills_.end();
  if (skill == skills_.end()) {
    OnTool(std::move(turn), index, std::move(args),
           Error("There is no such skill. Use a name from the list."));
    return;
  }
  base::FilePath path = skill->second;
  std::optional<std::string> file;
  if (const std::string* wanted = args.FindString("file")) {
    const base::FilePath relative = base::FilePath::FromUTF8Unsafe(*wanted);
    if (relative.empty() || relative.IsAbsolute() ||
        relative.ReferencesParent()) {
      OnTool(std::move(turn), index, std::move(args),
             Error("file must be a path inside the skill's folder."));
      return;
    }
    path = path.DirName().Append(relative);
    file = *wanted;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ReadSkill, std::move(path), skill->first,
                     std::move(file)),
      base::BindOnce(&AgentLoop::OnTool, weak_factory_.GetWeakPtr(),
                     std::move(turn), index, std::move(args)));
}

void AgentLoop::OnTool(ModelTurn turn,
                       size_t index,
                       base::DictValue args,
                       base::DictValue result) {
  const ToolCall& call = turn.calls[index];
  Step step;
  step.number = context_->AddResult(call.id, call.name, args, result);
  step.kind = Step::Kind::kTool;
  step.tool = call.name;
  step.args = std::move(args);
  step.result = std::move(result);
  on_step_.Run(step);
  RunCall(std::move(turn), index + 1);
}

void AgentLoop::Finish(Step step) {
  context_->ShortenAll();
  working_ = false;
  on_step_.Run(step);
}

}  // namespace uw
