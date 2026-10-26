// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/webui/uw_terminal/uw_terminal.mojom.h"

namespace uw {

class UwTerminalHandler;
class UwTerminalUI;

class UwTerminalUIConfig : public content::DefaultWebUIConfig<UwTerminalUI> {
 public:
  UwTerminalUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kTerminalHost) {}
};

// The controller of the uw://terminal page: one terminal tab.
class UwTerminalUI : public ui::MojoWebUIController,
                     public mojom::TerminalHandlerFactory {
 public:
  explicit UwTerminalUI(content::WebUI* web_ui);
  UwTerminalUI(const UwTerminalUI&) = delete;
  UwTerminalUI& operator=(const UwTerminalUI&) = delete;
  ~UwTerminalUI() override;

  void BindInterface(
      mojo::PendingReceiver<mojom::TerminalHandlerFactory> receiver);

 private:
  // mojom::TerminalHandlerFactory:
  void CreateHandler(
      mojo::PendingRemote<mojom::TerminalPage> page,
      mojo::PendingReceiver<mojom::TerminalHandler> handler) override;

  std::unique_ptr<UwTerminalHandler> handler_;
  mojo::Receiver<mojom::TerminalHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_TERMINAL_UW_TERMINAL_UI_H_
