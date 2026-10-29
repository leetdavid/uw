// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_new_tab_ui.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/grit/uw_chat_resources.h"
#include "chrome/grit/uw_chat_resources_map.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_chat/uw_new_tab_handler.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_handler.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

namespace uw {

UwNewTabUI::UwNewTabUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUINewTabPageHost);
  webui::SetupWebUIDataSource(source, kUwChatResources,
                              IDR_UW_CHAT_NEWTAB_HTML);
  // As on the chat page; see trusted-types.ts.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types default;");
  // The suggestions' icons, as in the command bar.
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
}

UwNewTabUI::~UwNewTabUI() = default;

void UwNewTabUI::BindInterface(
    mojo::PendingReceiver<mojom::NewTabHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwNewTabUI::BindInterface(
    mojo::PendingReceiver<searchbox::mojom::PageHandlerFactory> receiver) {
  search_factory_.reset();
  search_factory_.Bind(std::move(receiver));
}

void UwNewTabUI::BindInterface(
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver) {
  providers_ = std::make_unique<UwProvidersHandler>(
      Profile::FromWebUI(web_ui()), web_ui()->GetWebContents(),
      std::move(receiver));
}

void UwNewTabUI::BindInterface(
    mojo::PendingReceiver<mojom::ProblemReporter> receiver) {
  problem_reporter_ =
      std::make_unique<ProblemReporter>(web_ui(), std::move(receiver));
}

void UwNewTabUI::CreateNewTabHandler(
    mojo::PendingRemote<mojom::NewTabPage> page,
    mojo::PendingReceiver<mojom::NewTabHandler> handler) {
  handler_.reset();
  handler_ = std::make_unique<UwNewTabHandler>(web_ui(), std::move(page),
                                               std::move(handler));
}

void UwNewTabUI::CreatePageHandler(
    mojo::PendingRemote<searchbox::mojom::Page> page,
    mojo::PendingReceiver<searchbox::mojom::PageHandler> handler) {
  search_ = std::make_unique<UwCommandBarHandler>(
      std::move(handler), std::move(page), Profile::FromWebUI(web_ui()),
      web_ui()->GetWebContents(),
      base::BindRepeating(
          []() -> contextual_search::ContextualSearchSessionHandle* {
            return nullptr;
          }));
  // A picked row opens here, where the box is.
  search_->SetDisposition(WindowOpenDisposition::CURRENT_TAB);
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwNewTabUI)

}  // namespace uw
