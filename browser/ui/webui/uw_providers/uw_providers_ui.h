// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers.mojom.h"

namespace uw {

inline constexpr char kAiHost[] = "ai";

class UwProvidersHandler;
class UwProvidersUI;

class UwProvidersUIConfig : public content::DefaultWebUIConfig<UwProvidersUI> {
 public:
  UwProvidersUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kAiHost) {}
};

// The controller of uw://ai: the AI providers and the models that pick from
// them, skills, and connectors. See docs/plans/connectors.md#the-ai-page.
class UwProvidersUI : public ui::MojoWebUIController {
 public:
  explicit UwProvidersUI(content::WebUI* web_ui);
  UwProvidersUI(const UwProvidersUI&) = delete;
  UwProvidersUI& operator=(const UwProvidersUI&) = delete;
  ~UwProvidersUI() override;

  void BindInterface(mojo::PendingReceiver<mojom::ProvidersHandler> receiver);

 private:
  std::unique_ptr<UwProvidersHandler> handler_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_PROVIDERS_UW_PROVIDERS_UI_H_
