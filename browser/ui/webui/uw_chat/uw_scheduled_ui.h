// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "uw/browser/problem_reports/problem_reporter.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers.mojom.h"

namespace uw {

inline constexpr char kScheduledHost[] = "scheduled";
inline constexpr char kScheduledUrl[] = "chrome://scheduled/";

class UwProvidersHandler;
class UwScheduledHandler;
class UwScheduledUI;

class UwScheduledUIConfig : public content::DefaultWebUIConfig<UwScheduledUI> {
 public:
  UwScheduledUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kScheduledHost) {}
};

// The controller of uw://scheduled: every watched page and scheduled chat.
// It is a page of the uw://chat app. See docs/product.md, "Scheduled chats".
class UwScheduledUI : public ui::MojoWebUIController,
                      public mojom::ScheduledHandlerFactory {
 public:
  explicit UwScheduledUI(content::WebUI* web_ui);
  UwScheduledUI(const UwScheduledUI&) = delete;
  UwScheduledUI& operator=(const UwScheduledUI&) = delete;
  ~UwScheduledUI() override;

  void BindInterface(
      mojo::PendingReceiver<mojom::ScheduledHandlerFactory> receiver);
  // The cards' model menu lists the providers and their models.
  void BindInterface(mojo::PendingReceiver<mojom::ProvidersHandler> receiver);
  // uw: problems on the page go to PostHog, after a Yes. See
  // uw/browser/problem_reports/problem_reports.h.
  void BindInterface(mojo::PendingReceiver<mojom::ProblemReporter> receiver);

 private:
  // mojom::ScheduledHandlerFactory:
  void CreateScheduledHandler(
      mojo::PendingRemote<mojom::ScheduledPage> page,
      mojo::PendingReceiver<mojom::ScheduledHandler> handler) override;

  std::unique_ptr<UwScheduledHandler> handler_;
  std::unique_ptr<UwProvidersHandler> providers_;
  std::unique_ptr<ProblemReporter> problem_reporter_;
  mojo::Receiver<mojom::ScheduledHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_UI_H_
