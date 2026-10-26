// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_chat_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "chrome/grit/uw_chat_resources_map.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

namespace uw {

UwChatUI::UwChatUI(content::WebUI* web_ui) : ui::MojoWebUIController(web_ui) {
  content::WebContents* contents = web_ui->GetWebContents();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      contents->GetBrowserContext(), kChatHost);
  webui::SetupWebUIDataSource(source, kUwChatResources, IDR_UW_CHAT_CHAT_HTML);
  // The page's default policy lets one fixed style of Radix's scroll area
  // through and refuses every other HTML string; see trusted-types.ts.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types default;");
  // pdf.js reads a PDF in its worker, which the page serves itself.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::WorkerSrc, "worker-src 'self';");
  // Memory saver would end the chat with the page while the agent works.
  performance_manager::PageLiveStateDecorator::SetIsAutoDiscardable(contents,
                                                                    false);
}

UwChatUI::~UwChatUI() = default;

void UwChatUI::BindInterface(
    mojo::PendingReceiver<mojom::ChatHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwChatUI::CreateHandler(
    mojo::PendingRemote<mojom::ChatPage> page,
    mojo::PendingReceiver<mojom::ChatHandler> handler) {
  // A reload lets go of the chat before the page takes it again.
  handler_.reset();
  handler_ = std::make_unique<UwChatHandler>(
      web_ui()->GetWebContents(), std::move(page), std::move(handler));
}

void UwChatUI::BindInterface(
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver) {
  providers_ = std::make_unique<UwProvidersHandler>(
      Profile::FromWebUI(web_ui()), web_ui()->GetWebContents(),
      std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwChatUI)

}  // namespace uw
