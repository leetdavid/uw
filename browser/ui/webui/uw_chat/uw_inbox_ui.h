// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/problem_reports/problem_reporter.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

namespace uw {

inline constexpr char kInboxHost[] = "uw-inbox.top-chrome";
inline constexpr char kInboxUrl[] = "chrome://uw-inbox.top-chrome/";

class UwInboxHandler;
class UwInboxUI;

class UwInboxUIConfig : public DefaultTopChromeWebUIConfig<UwInboxUI> {
 public:
  UwInboxUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, kInboxHost) {}

  // The bubble takes the page's size.
  bool ShouldAutoResizeHost() override;
};

// The Inbox's page, in a bubble under the Inbox button at the top of the
// sidebar. It is a page of the uw://chat app. See docs/product.md, "Scheduled
// chats".
class UwInboxUI : public TopChromeWebUIController,
                  public mojom::InboxHandlerFactory {
 public:
  explicit UwInboxUI(content::WebUI* web_ui);
  UwInboxUI(const UwInboxUI&) = delete;
  UwInboxUI& operator=(const UwInboxUI&) = delete;
  ~UwInboxUI() override;

  static constexpr std::string_view GetWebUIName() { return "UwInbox"; }

  void BindInterface(
      mojo::PendingReceiver<mojom::InboxHandlerFactory> receiver);
  // uw: problems on the page go to PostHog, after a Yes. See
  // uw/browser/problem_reports/problem_reports.h.
  void BindInterface(mojo::PendingReceiver<mojom::ProblemReporter> receiver);

 private:
  // mojom::InboxHandlerFactory:
  void CreateInboxHandler(
      mojo::PendingRemote<mojom::InboxPage> page,
      mojo::PendingReceiver<mojom::InboxHandler> handler) override;

  std::unique_ptr<UwInboxHandler> handler_;
  std::unique_ptr<ProblemReporter> problem_reporter_;
  mojo::Receiver<mojom::InboxHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_UI_H_
