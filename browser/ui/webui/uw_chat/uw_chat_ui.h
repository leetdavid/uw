// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

namespace uw {

class UwChatHandler;
class UwChatUI;

class UwChatUIConfig : public content::DefaultWebUIConfig<UwChatUI> {
 public:
  UwChatUIConfig() : DefaultWebUIConfig(content::kChromeUIScheme, kChatHost) {}
};

// The controller of the uw://chat page: one chat with the agent.
class UwChatUI : public ui::MojoWebUIController,
                 public mojom::ChatHandlerFactory {
 public:
  explicit UwChatUI(content::WebUI* web_ui);
  UwChatUI(const UwChatUI&) = delete;
  UwChatUI& operator=(const UwChatUI&) = delete;
  ~UwChatUI() override;

  void BindInterface(mojo::PendingReceiver<mojom::ChatHandlerFactory> receiver);

 private:
  // mojom::ChatHandlerFactory:
  void CreateHandler(
      mojo::PendingRemote<mojom::ChatPage> page,
      mojo::PendingReceiver<mojom::ChatHandler> handler) override;

  std::unique_ptr<UwChatHandler> handler_;
  mojo::Receiver<mojom::ChatHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_UI_H_
