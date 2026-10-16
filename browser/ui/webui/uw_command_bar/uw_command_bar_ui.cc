// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/grit/uw_command_bar_resources.h"
#include "chrome/grit/uw_command_bar_resources_map.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_handler.h"

namespace uw {

bool UwCommandBarUIConfig::ShouldAutoResizeHost() {
  return true;
}

UwCommandBarUI::UwCommandBarUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kCommandBarHost);
  webui::SetupWebUIDataSource(source, kUwCommandBarResources,
                              IDR_UW_COMMAND_BAR_COMMAND_BAR_HTML);
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
}

UwCommandBarUI::~UwCommandBarUI() = default;

void UwCommandBarUI::BindInterface(
    mojo::PendingReceiver<searchbox::mojom::PageHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwCommandBarUI::CreatePageHandler(
    mojo::PendingRemote<searchbox::mojom::Page> page,
    mojo::PendingReceiver<searchbox::mojom::PageHandler> handler) {
  handler_ = std::make_unique<UwCommandBarHandler>(
      std::move(handler), std::move(page), Profile::FromWebUI(web_ui()),
      web_ui()->GetWebContents(), GetSessionHandleCallback());
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwCommandBarUI)

}  // namespace uw
