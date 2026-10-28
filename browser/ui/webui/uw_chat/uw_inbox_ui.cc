// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_inbox_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "chrome/grit/uw_chat_resources_map.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_chat/uw_inbox_handler.h"

namespace uw {

bool UwInboxUIConfig::ShouldAutoResizeHost() {
  return true;
}

UwInboxUI::UwInboxUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kInboxHost);
  webui::SetupWebUIDataSource(source, kUwChatResources,
                              IDR_UW_CHAT_INBOX_HTML);
  // As on the chat page; see trusted-types.ts.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types default;");
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
