// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_terminal/uw_terminal_ui.h"

#include <utility>

#include "chrome/grit/uw_terminal_resources.h"
#include "chrome/grit/uw_terminal_resources_map.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/webui/uw_terminal/uw_terminal_handler.h"

namespace uw {

UwTerminalUI::UwTerminalUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebContents* contents = web_ui->GetWebContents();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      contents->GetBrowserContext(), kTerminalHost);
  webui::SetupWebUIDataSource(source, kUwTerminalResources,
                              IDR_UW_TERMINAL_TERMINAL_HTML);
  // ghostty-web is WebAssembly, which its script reads from a data: address.
  // Nothing else about the policy of a trusted page is loosened.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources chrome://webui-test 'self' "
      "'wasm-unsafe-eval';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src chrome://webui-test chrome://resources chrome://theme "
      "'self' data:;");
  // Memory saver would end the shell with the page.
  performance_manager::PageLiveStateDecorator::SetIsAutoDiscardable(contents,
                                                                    false);
}

UwTerminalUI::~UwTerminalUI() = default;

void UwTerminalUI::BindInterface(
    mojo::PendingReceiver<mojom::TerminalHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwTerminalUI::CreateHandler(
    mojo::PendingRemote<mojom::TerminalPage> page,
    mojo::PendingReceiver<mojom::TerminalHandler> handler) {
  handler_ = std::make_unique<UwTerminalHandler>(
      web_ui()->GetWebContents(), std::move(page), std::move(handler));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwTerminalUI)

}  // namespace uw
