// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_watch_card_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "chrome/grit/uw_chat_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

namespace uw {

bool UwWatchCardUIConfig::ShouldAutoResizeHost() {
  return true;
}

UwWatchCardUI::UwWatchCardUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kWatchCardHost);
  webui::SetupWebUIDataSource(source, kUwChatResources,
                              IDR_UW_CHAT_WATCH_CARD_HTML);
  // As on the chat page; see trusted-types.ts.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types default;");
}

UwWatchCardUI::~UwWatchCardUI() = default;

void UwWatchCardUI::BindInterface(
    mojo::PendingReceiver<mojom::WatchCardHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwWatchCardUI::BindInterface(
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver) {
  providers_ = std::make_unique<UwProvidersHandler>(
      Profile::FromWebUI(web_ui()), web_ui()->GetWebContents(),
      std::move(receiver));
}

void UwWatchCardUI::CreateWatchCardHandler(
    mojo::PendingRemote<mojom::WatchCardPage> page,
    mojo::PendingReceiver<mojom::WatchCardHandler> handler) {
  handler_ = std::make_unique<UwWatchCardHandler>(this, std::move(page),
                                                  std::move(handler));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwWatchCardUI)

}  // namespace uw
