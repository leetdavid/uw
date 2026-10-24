// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
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
#include "uw/browser/agent/command_check.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/components/agent/run_report.h"
#include "uw/components/agent/schedule_tool.h"

namespace uw {
namespace {

// A chat that takes this many turns without an answer has lost its way.
constexpr int kMaxTurns = 40;
// A large model can think for a while before it answers.
constexpr base::TimeDelta kModelTimeout = base::Minutes(2);
// Long enough for a dozen full readings, short enough for most models.
constexpr size_t kMaxRequestLength = 400000;
// Fewer characters than a token usually holds, so a request kept under a
// model's input limit by this count stays under it in tokens.
constexpr int64_t kCharsPerToken = 3;

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

AgentLoop::Step StoppedByUser() {
  AgentLoop::Step step;
  step.kind = AgentLoop::Step::Kind::kStoppedByUser;
  step.text = "You stopped the chat.";
  return step;
}

}  // namespace

std::optional<AgentModel> AgentModelFor(Profile& profile,
                                        const ModelChoice& choice) {
  const PrefService& prefs = *profile.GetPrefs();
  AgentModel model;
  model.connection = ConfigForChoice(prefs, choice);
  if (!IsComplete(model.connection)) {
    return std::nullopt;
  }
  if (!model.connection.key_account.empty()) {
    model.api_key =
        ApiKeyStore().GetKey(model.connection.key_account).value_or("");
  }
  const ModelCatalog* catalog = CatalogStore::For(&profile)->Current();
  const std::optional<Provider> provider =
      FindProvider(prefs, choice.provider_id);
  if (catalog && provider) {
    const ModelAbilities abilities = catalog->For(*provider, choice.model);
    model.price = abilities.price;
    model.input_limit = abilities.input_limit;
  }
  return model;
}

AgentLoop::AgentLoop(tabs::TabInterface& chat,
                     AgentModel model,
                     OnStep on_step,
                     std::unique_ptr<AgentContext> context,
                     base::RepeatingClosure on_change)
    : tools_(chat),
      profile_(chat.GetBrowserWindowInterface()->GetProfile()),
      model_(std::move(model)),
      on_step_(std::move(on_step)),
      loader_(profile_->GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess()),
      context_(context ? std::move(context) : std::make_unique<AgentContext>()),
      on_change_(std::move(on_change)) {}

AgentLoop::~AgentLoop() = default;

void AgentLoop::SetReport(Report report) {
  report_ = std::move(report);
  UpdateConnectorTools();
}

void AgentLoop::SetScheduleCard(FillScheduleCard fill) {
  schedule_card_ = std::move(fill);
  UpdateConnectorTools();
}

void AgentLoop::SetTriggers(AgentTools::StartTrigger start,
                            AgentTools::StopTrigger stop) {
  tools_.SetTriggers(std::move(start), std::move(stop));
  UpdateConnectorTools();
}

void AgentLoop::SetSkills(const std::vector<Skill>& skills) {
  skills_ = started_skills_;
  std::string list;
  for (const Skill& skill : skills) {
    if (!skill.for_agent) {
      continue;
    }
    skills_[skill.name] = skill.file;
    base::StrAppend(&list, {list.empty() ? "" : "\n", "- ", skill.name, ": ",
                            skill.description});
  }
  context_->SetSkills(list);
}

void AgentLoop::Send(std::string_view text, std::vector<std::string> images) {
  if (working_) {
    return;
  }
  working_ = true;
  turns_ = 0;
  context_->AddUser(text, images);
  AskModel();
}

void AgentLoop::Resume() {
  Send(
      "uw quit while you were working and has started again. Your tabs may "
      "have reloaded or closed. Check where things stand before you go on, "
      "and do not repeat a step that may already have happened.");
}

void AgentLoop::TryAgain() {
  if (working_) {
    return;
  }
  working_ = true;
  turns_ = 0;
  AskModel();
}

void AgentLoop::Stop() {
  if (!working_) {
    return;
  }
  if (asking_) {
    client_.reset();
    asking_ = false;
    Finish(StoppedByUser());
    return;
  }
  stopping_ = true;
  tools_.EndWait();
  EndCheck();
}

void AgentLoop::Steer(std::string_view text, std::vector<std::string> images) {
  if (!working_) {
    Send(text, std::move(images));
    return;
  }
  turns_ = 0;
  if (asking_) {
    client_.reset();
    asking_ = false;
    context_->AddUser(text, images);
    AskModel();
    return;
  }
  steer_ = Message{std::string(text), std::move(images)};
  // A command left running is the agent's to look at with the message.
  tools_.EndWait();
  EndCheck();
}

void AgentLoop::AskModel() {
  if (stopping_) {
    Finish(StoppedByUser());
    return;
  }
  if (!recalled_images_.empty()) {
    context_->ShowImages(std::exchange(recalled_images_, {}));
  }
  for (const auto& [tool, images] : std::exchange(tool_images_, {})) {
    context_->AddToolImages(tool, images);
  }
  if (steer_) {
    const Message steer = *std::exchange(steer_, std::nullopt);
    context_->AddUser(steer.text, steer.images);
  }
  if (++turns_ > kMaxTurns) {
    Finish(Failed(base::StrCat({"The agent stopped after ",
                                base::NumberToString(kMaxTurns),
                                " steps without an answer."})));
    return;
  }
  const std::optional<std::string> body =
      context_->Request(model_.connection.model, MaxRequestLength());
  if (!body || model_.connection.endpoint.empty()) {
    Step failed = Failed("No agent model is set.");
    failed.model_unreachable = true;
    Finish(std::move(failed));
    return;
  }
  client_ = RemoteModelClient::ForLanguageModel(
      loader_, model_.connection, model_.api_key, *body, kModelTimeout);
  asking_ = true;
  client_->Send(
      base::BindOnce(&AgentLoop::OnModel, weak_factory_.GetWeakPtr()));
}

void AgentLoop::OnModel(RemoteModelClient::Outcome outcome) {
  asking_ = false;
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
    Step failed = Failed(std::move(text));
    failed.model_unreachable = true;
    Finish(std::move(failed));
    return;
  }
  if (std::optional<ModelUsage> usage = UsageInAnswer(outcome.body)) {
    UsageRecord record;
    record.time = base::Time::Now();
    record.model = model_.connection.model;
    record.usage = *usage;
    record.cost = CostOf(*usage, model_.price);
    record.price = model_.price;
    record.parts = context_->last_parts();
    record.max_length = static_cast<int64_t>(MaxRequestLength());
    record.input_limit = model_.input_limit;
    record.made_room = context_->last_made_room();
    record.message = context_->UserMessages();
    context_->AddUsage(std::move(record));
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
  if (stopping_) {
    context_->CloseOpenCalls("The user stopped the chat.");
    Finish(StoppedByUser());
    return;
  }
  // The user wrote in: the rest of the answer's steps wait for the model to
  // read it.
  if (steer_ && index < turn.calls.size()) {
    context_->CloseOpenCalls("Not run, because the user sent a new message.");
    index = turn.calls.size();
  }
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
    base::DictValue recalled =
        context_->RecallWithin(*args, MaxRequestLength());
    if (const std::optional<int> image = recalled.FindInt("image")) {
      recalled_images_.push_back(*image);
    }
    OnTool(std::move(turn), index, std::move(*args), std::move(recalled));
    return;
  }
  if (call.name == "skill") {
    LoadSkill(std::move(turn), index, std::move(*args));
    return;
  }
  if (call.name == kReportTool && report_) {
    base::DictValue answer = report_.Run(*args);
    OnTool(std::move(turn), index, std::move(*args), std::move(answer));
    return;
  }
  if (call.name == kScheduleTool && schedule_card_) {
    base::DictValue answer = schedule_card_.Run(*args);
    OnTool(std::move(turn), index, std::move(*args), std::move(answer));
    return;
  }
  if (call.name == kLoadConnectorTool) {
    LoadConnector(std::move(turn), index, std::move(*args));
    return;
  }
  if (auto tool = connector_tools_.find(call.name);
      tool != connector_tools_.end()) {
    if (!approve_ || !ConnectorNeedsApproval(call.name)) {
      CallConnector(std::move(turn), index, std::move(*args));
      return;
    }
    AgentTools::Approval approval;
    approval.connector = tool->second.connector;
    approval.tool = tool->second.tool.name;
    approval.action = base::StrCat({"Run ", approval.tool});
    approval.arguments = base::WriteJsonWithOptions(
                             *args, base::JSONWriter::OPTIONS_PRETTY_PRINT)
                             .value_or("{}");
    approval.reason =
        mode_ == ApprovalMode::kAskFirst
            ? "Ask first asks before every connector tool."
            : "This tool doesn't say it only reads, so Auto asks.";
    approve_.Run(
        std::move(approval),
        base::BindOnce(&AgentLoop::OnApproval, weak_factory_.GetWeakPtr(),
                       std::move(turn), index, std::move(*args)));
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
  // Full auto still asks the command check about each terminal step.
  const bool terminal = AgentTools::IsTerminalTool(call.name);
  if (approve_ && (mode_ != ApprovalMode::kFullAuto || terminal)) {
    if (std::optional<AgentTools::Approval> approval = tools_.ApprovalFor(
            call.name, *args,
            /*every_step=*/mode_ == ApprovalMode::kAskFirst)) {
      if (terminal && mode_ != ApprovalMode::kAskFirst) {
        CheckStep(std::move(turn), index, std::move(*args),
                  std::move(*approval));
        return;
      }
      approve_.Run(
          std::move(*approval),
          base::BindOnce(&AgentLoop::OnApproval, weak_factory_.GetWeakPtr(),
                         std::move(turn), index, std::move(*args)));
      return;
    }
  }
  CallTool(std::move(turn), index, std::move(*args));
}

void AgentLoop::CheckStep(ModelTurn turn,
                          size_t index,
                          base::DictValue args,
                          AgentTools::Approval approval) {
  CommandCheckInput input;
  input.chat = context_->UserAndActions(turn.calls[index].id);
  input.step.tab = approval.tab;
  input.step.folder = approval.folder;
  input.step.command = approval.command;
  input.step.text = approval.text;
  input.step.keys = approval.keys;
  input.file_only = mode_ == ApprovalMode::kFullAuto;
  check_ = std::make_unique<Check>();
  check_->turn = std::move(turn);
  check_->index = index;
  check_->args = std::move(args);
  check_->approval = std::move(approval);
  check_->file_only = input.file_only;
  check_->client = CommandCheck::Send(
      *profile_, model_.connection, model_.api_key, input,
      base::BindOnce(&AgentLoop::OnCheck, weak_factory_.GetWeakPtr()));
  if (!check_->client) {
    OnCheck(std::nullopt);
  }
}

size_t AgentLoop::MaxRequestLength() const {
  if (model_.input_limit <= 0) {
    return kMaxRequestLength;
  }
  return static_cast<size_t>(std::min<int64_t>(
      kMaxRequestLength, model_.input_limit * kCharsPerToken));
}

void AgentLoop::OnCheck(std::optional<CommandCheckAnswer> answer) {
  std::unique_ptr<Check> check = std::move(check_);
  if (check->client && check->client->usage()) {
    UsageRecord record;
    record.kind = UsageRecord::Kind::kCommandCheck;
    record.time = base::Time::Now();
    record.model = check->client->model();
    record.usage = *check->client->usage();
    record.cost = CostOf(record.usage, check->client->price());
    record.price = check->client->price();
    context_->AddUsage(std::move(record));
  }
  if (check->client) {
    // The client that answered may still be on the stack.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(check->client));
  }
  const CommandCheckVerdict verdict =
      JudgeTerminalStep(answer, check->file_only, model_.connection.model);
  if (!verdict.ask) {
    CallTool(std::move(check->turn), check->index, std::move(check->args));
    return;
  }
  AgentTools::Approval approval = std::move(check->approval);
  approval.reason = verdict.reason;
  approval.sensitive = verdict.sensitive;
  approval.file = verdict.file;
  approve_.Run(
      std::move(approval),
      base::BindOnce(&AgentLoop::OnApproval, weak_factory_.GetWeakPtr(),
                     std::move(check->turn), check->index,
                     std::move(check->args)));
}

void AgentLoop::EndCheck() {
  if (!check_) {
    return;
  }
  std::unique_ptr<Check> check = std::move(check_);
  RunCall(std::move(check->turn), check->index);
}

void AgentLoop::SetApprover(Approve approve) {
  approve_ = std::move(approve);
  tools_.SetConfirmSite(
      base::BindRepeating(&AgentLoop::ConfirmSite, weak_factory_.GetWeakPtr()));
}

void AgentLoop::CallTool(ModelTurn turn, size_t index, base::DictValue args) {
  const std::string name = turn.calls[index].name;
  base::DictValue asked = args.Clone();
  tools_.Call(
      name, args,
      base::BindOnce(&AgentLoop::OnToolCalled, weak_factory_.GetWeakPtr(),
                     std::move(turn), index, std::move(asked)));
}

void AgentLoop::ConfirmSite(AgentTools::Approval approval,
                            base::OnceCallback<void(bool)> done) {
  if (mode_ == ApprovalMode::kFullAuto) {
    std::move(done).Run(true);
    return;
  }
  approve_.Run(std::move(approval),
               base::BindOnce(&AgentLoop::OnSiteAnswer,
                              weak_factory_.GetWeakPtr(), std::move(done)));
}

void AgentLoop::OnSiteAnswer(base::OnceCallback<void(bool)> done,
                             ApprovalAnswer answer) {
  const bool approved = answer.choice == ApprovalAnswer::Choice::kApprove;
  if (!approved) {
    site_refused_ = std::move(answer);
  }
  std::move(done).Run(approved);
}

void AgentLoop::OnToolCalled(ModelTurn turn,
                             size_t index,
                             base::DictValue args,
                             base::DictValue result) {
  const std::optional<ApprovalAnswer> refused =
      std::exchange(site_refused_, std::nullopt);
  if (refused && refused->choice == ApprovalAnswer::Choice::kStop) {
    context_->CloseOpenCalls("The user stopped the chat.");
    Finish(StoppedByUser());
    return;
  }
  if (refused) {
    result = Error(
        "The user did not approve acting on this site, so the step did not "
        "happen.");
    if (!refused->note.empty()) {
      result.Set("user_said", refused->note);
    }
  }
  OnTool(std::move(turn), index, std::move(args), std::move(result));
}

void AgentLoop::OnApproval(ModelTurn turn,
                           size_t index,
                           base::DictValue args,
                           ApprovalAnswer answer) {
  const std::string name = turn.calls[index].name;
  const auto connector_tool = connector_tools_.find(name);
  const bool connector = connector_tool != connector_tools_.end();
  switch (answer.choice) {
    case ApprovalAnswer::Choice::kAllowForChat:
    case ApprovalAnswer::Choice::kAlwaysAllow:
      if (!connector) {
        CallTool(std::move(turn), index, std::move(args));
        return;
      }
      if (answer.choice == ApprovalAnswer::Choice::kAlwaysAllow) {
        Connectors::Get().SetAlwaysAllowed(connector_tool->second.connector,
                                           connector_tool->second.tool.name,
                                           true);
      } else {
        context_->AllowForChat(connector_tool->second.connector,
                               connector_tool->second.tool.name);
      }
      CallConnector(std::move(turn), index, std::move(args));
      return;
    case ApprovalAnswer::Choice::kApprove:
      if (connector) {
        CallConnector(std::move(turn), index, std::move(args));
      } else {
        CallTool(std::move(turn), index, std::move(args));
      }
      return;
    case ApprovalAnswer::Choice::kDecline: {
      base::DictValue result =
          Error("The user did not approve this step, so it did not happen.");
      if (!answer.note.empty()) {
        result.Set("user_said", answer.note);
      }
      OnTool(std::move(turn), index, std::move(args), std::move(result));
      return;
    }
    case ApprovalAnswer::Choice::kStop:
      context_->CloseOpenCalls("The user stopped the chat.");
      Finish(StoppedByUser());
      return;
  }
}

void AgentLoop::SetConnectors(std::vector<ConnectorTools> connectors) {
  connectors_ = std::move(connectors);
  context_->SetConnectors(ConnectorsForInstructions(connectors_));
  UpdateConnectorTools();
}

void AgentLoop::UpdateConnectorTools() {
  connector_tools_.clear();
  base::ListValue tools;
  if (report_) {
    tools.Append(ReportToolSchema());
  }
  if (schedule_card_) {
    tools.Append(ScheduleToolSchema());
  }
  if (tools_.takes_triggers()) {
    tools.Append(TriggerToolSchema());
    tools.Append(StopTriggerToolSchema());
  }
  if (!connectors_.empty()) {
    tools.Append(LoadConnectorToolSchema());
  }
  for (const ConnectorTools& connector : connectors_) {
    if (!context_->loaded_connectors().contains(connector.name)) {
      continue;
    }
    for (const mcp::Tool& tool : connector.tools) {
      const std::string name = ModelToolName(connector.name, tool.name);
      if (connector_tools_.contains(name)) {
        continue;
      }
      connector_tools_[name] = {connector.name, tool};
      tools.Append(ModelToolSchema(connector.name, tool));
    }
  }
  context_->SetExtraTools(std::move(tools));
}

void AgentLoop::LoadConnector(ModelTurn turn,
                              size_t index,
                              base::DictValue args) {
  const std::string* name = args.FindString("name");
  const auto connector =
      name ? std::ranges::find(connectors_, *name, &ConnectorTools::name)
           : connectors_.end();
  if (connector == connectors_.end()) {
    OnTool(std::move(turn), index, std::move(args),
           Error("There is no connector by that name. Use a name from the "
                 "list."));
    return;
  }
  context_->LoadConnector(*name);
  UpdateConnectorTools();
  base::ListValue tools;
  for (const mcp::Tool& tool : connector->tools) {
    tools.Append(ModelToolName(connector->name, tool.name));
  }
  OnTool(
      std::move(turn), index, std::move(args),
      base::DictValue().Set("connector", *name).Set("tools", std::move(tools)));
}

bool AgentLoop::ConnectorNeedsApproval(const std::string& name) const {
  const ConnectorTool& tool = connector_tools_.at(name);
  if (mode_ == ApprovalMode::kFullAuto ||
      context_->AllowedForChat(tool.connector, tool.tool.name)) {
    return false;
  }
  if (const Connectors::Info* info = Connectors::Get().Find(tool.connector);
      info && info->always_allowed.contains(tool.tool.name)) {
    return false;
  }
  return mode_ == ApprovalMode::kAskFirst || !tool.tool.read_only;
}

void AgentLoop::CallConnector(ModelTurn turn,
                              size_t index,
                              base::DictValue args) {
  const ConnectorTool& tool = connector_tools_.at(turn.calls[index].name);
  const std::string connector = tool.connector;
  const std::string name = tool.tool.name;
  base::DictValue asked = args.Clone();
  Connectors::Get().CallTool(
      connector, name, std::move(args),
      base::BindOnce(&AgentLoop::OnConnectorAnswer, weak_factory_.GetWeakPtr(),
                     std::move(turn), index, std::move(asked)));
}

void AgentLoop::OnConnectorAnswer(
    ModelTurn turn,
    size_t index,
    base::DictValue args,
    base::expected<mcp::Content, std::string> answer) {
  if (!answer.has_value()) {
    OnTool(std::move(turn), index, std::move(args), Error(answer.error()));
    return;
  }
  base::DictValue result;
  if (answer->failed) {
    result.Set("error",
               answer->text.empty() ? "The tool failed." : answer->text);
  } else {
    result.Set("text", answer->text);
  }
  if (!answer->images.empty()) {
    if (sees_images_) {
      result.Set("images", static_cast<int>(answer->images.size()));
      tool_images_.emplace_back(turn.calls[index].name,
                                std::move(answer->images));
    } else {
      result.Set("images", "[image left out: this model can't see images]");
    }
  }
  OnTool(std::move(turn), index, std::move(args), std::move(result));
}

void AgentLoop::StartedSkill(const Skill& skill) {
  started_skills_[skill.name] = skill.file;
  skills_[skill.name] = skill.file;
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
  // The steps after a failed or declined one in the same answer counted on
  // it, so they do not run.
  if (step.result.contains("error") && index + 1 < turn.calls.size()) {
    context_->CloseOpenCalls(
        base::StrCat({"Not run, because step ",
                      base::NumberToString(step.number), " did not work."}));
    index = turn.calls.size() - 1;
  }
  RunCall(std::move(turn), index + 1);
}

void AgentLoop::Finish(Step step) {
  stopping_ = false;
  steer_.reset();
  recalled_images_.clear();
  tool_images_.clear();
  working_ = false;
  on_step_.Run(step);
}

}  // namespace uw
