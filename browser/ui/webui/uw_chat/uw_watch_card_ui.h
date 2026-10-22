// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers.mojom.h"

namespace uw {

inline constexpr char kWatchCardHost[] = "uw-watch-card.top-chrome";
inline constexpr char kWatchCardUrl[] = "chrome://uw-watch-card.top-chrome/";

class UwProvidersHandler;
class UwWatchCardHandler;
class UwWatchCardUI;

class UwWatchCardUIConfig : public DefaultTopChromeWebUIConfig<UwWatchCardUI> {
 public:
  UwWatchCardUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, kWatchCardHost) {}

  // The bubble takes the page's size.
  bool ShouldAutoResizeHost() override;
};

// The watch card's page, in a bubble under the eye in the address bar. It is
// a page of the uw://chat app.
class UwWatchCardUI : public TopChromeWebUIController,
                      public mojom::WatchCardHandlerFactory {
 public:
  explicit UwWatchCardUI(content::WebUI* web_ui);
  UwWatchCardUI(const UwWatchCardUI&) = delete;
  UwWatchCardUI& operator=(const UwWatchCardUI&) = delete;
  ~UwWatchCardUI() override;

  static constexpr std::string_view GetWebUIName() { return "UwWatchCard"; }

  void BindInterface(
      mojo::PendingReceiver<mojom::WatchCardHandlerFactory> receiver);
  // The card's model menu lists the providers and their models.
  void BindInterface(mojo::PendingReceiver<mojom::ProvidersHandler> receiver);

 private:
  // mojom::WatchCardHandlerFactory:
  void CreateWatchCardHandler(
      mojo::PendingRemote<mojom::WatchCardPage> page,
      mojo::PendingReceiver<mojom::WatchCardHandler> handler) override;

  std::unique_ptr<UwWatchCardHandler> handler_;
  std::unique_ptr<UwProvidersHandler> providers_;
  mojo::Receiver<mojom::WatchCardHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_UI_H_
