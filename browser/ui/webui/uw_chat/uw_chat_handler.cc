// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"

namespace uw {
namespace {

mojom::StepPtr Failed(std::string text) {
  auto step = mojom::Step::New();
  step->kind = mojom::StepKind::kFailed;
  step->text = std::move(text);
  return step;
}

mojom::StepKind KindOf(AgentLoop::Step::Kind kind) {
  switch (kind) {
    case AgentLoop::Step::Kind::kTool:
      return mojom::StepKind::kTool;
    case AgentLoop::Step::Kind::kAnswer:
      return mojom::StepKind::kAnswer;
    case AgentLoop::Step::Kind::kFailed:
      return mojom::StepKind::kFailed;
  }
}

}  // namespace

UwChatHandler::UwChatHandler(content::WebContents* contents,
                             mojo::PendingRemote<mojom::ChatPage> page,
                             mojo::PendingReceiver<mojom::ChatHandler> receiver)
    : contents_(contents),
      page_(std::move(page)),
      receiver_(this, std::move(receiver)) {}

UwChatHandler::~UwChatHandler() = default;

void UwChatHandler::Send(const std::string& text) {
  if (agent_ || Start()) {
    agent_->Send(text);
  }
}

bool UwChatHandler::Start() {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!tab) {
    page_->OnStep(Failed("A chat works only in a tab."));
    return false;
  }
  const AiConnectionConfig config = ReadAgentModelConfig(
      Profile::FromBrowserContext(contents_->GetBrowserContext())->GetPrefs());
  if (!IsComplete(config)) {
    page_->OnStep(Failed("Set an agent model on uw's settings page first."));
    return false;
  }
  AgentModel model;
  model.url = ChatCompletionsUrl(config.endpoint);
  model.model = config.model;
  if (!config.key_account.empty()) {
    model.api_key =
        ApiKeyStore().GetKey(config.key_account).value_or(std::string());
  }
  agent_ = std::make_unique<AgentLoop>(
      *tab, std::move(model),
      base::BindRepeating(&UwChatHandler::OnStep, base::Unretained(this)));
  return true;
}

void UwChatHandler::OnStep(const AgentLoop::Step& step) {
  auto sent = mojom::Step::New();
  sent->kind = KindOf(step.kind);
  sent->tool = step.tool;
  sent->args = base::WriteJson(step.args).value_or("{}");
  sent->result = base::WriteJson(step.result).value_or("{}");
  sent->text = step.text;
  page_->OnStep(std::move(sent));
}

}  // namespace uw
