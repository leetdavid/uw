// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_ui.h"

namespace uw {

UwScheduledUI::UwScheduledUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  AddChatAppSource(web_ui, kScheduledHost, IDR_UW_CHAT_SCHEDULED_HTML);
}

UwScheduledUI::~UwScheduledUI() = default;

void UwScheduledUI::BindInterface(
    mojo::PendingReceiver<mojom::ScheduledHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwScheduledUI::CreateScheduledHandler(
    mojo::PendingRemote<mojom::ScheduledPage> page,
    mojo::PendingReceiver<mojom::ScheduledHandler> handler) {
  handler_.reset();
  handler_ = std::make_unique<UwScheduledHandler>(web_ui(), std::move(page),
                                                  std::move(handler));
}

void UwScheduledUI::BindInterface(
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver) {
  providers_ = std::make_unique<UwProvidersHandler>(
      Profile::FromWebUI(web_ui()), web_ui()->GetWebContents(),
      std::move(receiver));
}

void UwScheduledUI::BindInterface(
    mojo::PendingReceiver<mojom::ProblemReporter> receiver) {
  problem_reporter_ =
      std::make_unique<ProblemReporter>(web_ui(), std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwScheduledUI)

}  // namespace uw
