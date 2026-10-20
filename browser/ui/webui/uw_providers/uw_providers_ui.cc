// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_providers/uw_providers_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/uw_chat_resources.h"
#include "chrome/grit/uw_chat_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

namespace uw {

UwProvidersUI::UwProvidersUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), kAiHost);
  // The page is built with the chat page and ships in its pak.
  webui::SetupWebUIDataSource(source, kUwChatResources,
                              IDR_UW_CHAT_PROVIDERS_HTML);
  // Each section has its own address, such as uw://ai/skills, and
  // uw://ai/providers/<id> opens one provider.
  source->SetDefaultResource(IDR_UW_CHAT_PROVIDERS_HTML);
  // Lets one fixed style of Radix's scroll area through; see
  // trusted-types.ts.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types default;");
}

UwProvidersUI::~UwProvidersUI() = default;

void UwProvidersUI::BindInterface(
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver) {
  handler_ = std::make_unique<UwProvidersHandler>(Profile::FromWebUI(web_ui()),
                                                  web_ui()->GetWebContents(),
                                                  std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwProvidersUI)

}  // namespace uw
