// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_watch_card_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_ui.h"

namespace uw {

bool UwWatchCardUIConfig::ShouldAutoResizeHost() {
  return true;
}

UwWatchCardUI::UwWatchCardUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  AddChatAppSource(web_ui, kWatchCardHost, IDR_UW_CHAT_WATCH_CARD_HTML);
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

void UwWatchCardUI::BindInterface(
    mojo::PendingReceiver<mojom::ProblemReporter> receiver) {
  problem_reporter_ =
      std::make_unique<ProblemReporter>(web_ui(), std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwWatchCardUI)

}  // namespace uw
