// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_ui.h"

#include <memory>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/grit/uw_command_bar_resources.h"
#include "chrome/grit/uw_command_bar_resources_map.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/webui/uw_command_bar/exchange_rates.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_handler.h"

namespace uw {
namespace {

const void* const kActionsKey = &kActionsKey;

struct ActionsData : base::SupportsUserData::Data {
  raw_ptr<CommandBarActions> actions;
};

}  // namespace

CommandBarActions* GetCommandBarActions(content::WebContents* page) {
  auto* data = static_cast<ActionsData*>(page->GetUserData(kActionsKey));
  return data ? data->actions.get() : nullptr;
}

void SetCommandBarActions(content::WebContents* page,
                          CommandBarActions* actions) {
  auto data = std::make_unique<ActionsData>();
  data->actions = actions;
  page->SetUserData(kActionsKey, std::move(data));
}

void StartShellCommand(content::WebContents* page) {
  content::WebUI* web_ui = page->GetWebUI();
  if (UwCommandBarUI* ui =
          web_ui ? web_ui->GetController()->GetAs<UwCommandBarUI>() : nullptr) {
    if (ui->handler_) {
      ui->handler_->StartShellCommand();
    }
  }
}

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

void UwCommandBarUI::BindInterface(
    mojo::PendingReceiver<mojom::CalculatorHandler> receiver) {
  calculator_.reset();
  calculator_.Bind(std::move(receiver));
}

void UwCommandBarUI::GetCurrencies(GetCurrenciesCallback callback) {
  Profile* profile = Profile::FromWebUI(web_ui());
  ExchangeRates* rates = ExchangeRates::For(profile);
  std::string currency =
      profile->GetPrefs()->GetString(kCommandBarCurrencyPref);
  if (currency.empty()) {
    currency = RegionCurrency();
  }
  std::move(callback).Run(rates->Saved(), currency);
  rates->Refresh();
}

void UwCommandBarUI::CopyAnswer(const std::string& text) {
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(base::UTF8ToUTF16(text));
  if (embedder()) {
    embedder()->CloseUI();
  }
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
