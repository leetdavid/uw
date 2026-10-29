// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_UI_H_

#include <memory>

#include "chrome/common/webui_url_constants.h"
#include "components/omnibox/browser/searchbox.mojom.h"
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

class UwCommandBarHandler;
class UwNewTabHandler;
class UwNewTabUI;
class UwProvidersHandler;

// uw's page takes the address of Chromium's new tab page, which every new tab
// already loads, in normal and private profiles alike, so it counts as the
// new tab page wherever Chromium checks.
class UwNewTabUIConfig : public content::DefaultWebUIConfig<UwNewTabUI> {
 public:
  UwNewTabUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           chrome::kChromeUINewTabPageHost) {}
};

// The controller of uw's new tab page: a box to search or ask, and the state
// of uw's features. It is a page of the uw://chat app. See
// docs/plans/new-tab-page.md.
class UwNewTabUI : public ui::MojoWebUIController,
                   public mojom::NewTabHandlerFactory,
                   public searchbox::mojom::PageHandlerFactory {
 public:
  explicit UwNewTabUI(content::WebUI* web_ui);
  UwNewTabUI(const UwNewTabUI&) = delete;
  UwNewTabUI& operator=(const UwNewTabUI&) = delete;
  ~UwNewTabUI() override;

  void BindInterface(
      mojo::PendingReceiver<mojom::NewTabHandlerFactory> receiver);
  // The box's suggestions, as in the command bar.
  void BindInterface(
      mojo::PendingReceiver<searchbox::mojom::PageHandlerFactory> receiver);
  // The model chip lists the providers and their models.
  void BindInterface(mojo::PendingReceiver<mojom::ProvidersHandler> receiver);
  // uw: problems on the page go to PostHog, after a Yes. See
  // uw/browser/problem_reports/problem_reports.h.
  void BindInterface(mojo::PendingReceiver<mojom::ProblemReporter> receiver);

 private:
  // mojom::NewTabHandlerFactory:
  void CreateNewTabHandler(
      mojo::PendingRemote<mojom::NewTabPage> page,
      mojo::PendingReceiver<mojom::NewTabHandler> handler) override;

  // searchbox::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<searchbox::mojom::Page> page,
      mojo::PendingReceiver<searchbox::mojom::PageHandler> handler) override;

  std::unique_ptr<UwNewTabHandler> handler_;
  std::unique_ptr<UwCommandBarHandler> search_;
  std::unique_ptr<UwProvidersHandler> providers_;
  std::unique_ptr<ProblemReporter> problem_reporter_;
  mojo::Receiver<mojom::NewTabHandlerFactory> factory_{this};
  mojo::Receiver<searchbox::mojom::PageHandlerFactory> search_factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_UI_H_
