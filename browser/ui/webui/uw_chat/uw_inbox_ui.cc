// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_inbox_ui.h"

#include <utility>

#include "chrome/grit/uw_chat_resources.h"
#include "content/public/browser/web_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_inbox_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_ui.h"

namespace uw {

bool UwInboxUIConfig::ShouldAutoResizeHost() {
  return true;
}

UwInboxUI::UwInboxUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  AddChatAppSource(web_ui, kInboxHost, IDR_UW_CHAT_INBOX_HTML);
}

UwInboxUI::~UwInboxUI() = default;

void UwInboxUI::BindInterface(
    mojo::PendingReceiver<mojom::InboxHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwInboxUI::CreateInboxHandler(
    mojo::PendingRemote<mojom::InboxPage> page,
    mojo::PendingReceiver<mojom::InboxHandler> handler) {
  handler_.reset();
  handler_ =
      std::make_unique<UwInboxHandler>(this, std::move(page), std::move(handler));
}

void UwInboxUI::BindInterface(
    mojo::PendingReceiver<mojom::ProblemReporter> receiver) {
  problem_reporter_ =
      std::make_unique<ProblemReporter>(web_ui(), std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwInboxUI)

}  // namespace uw
