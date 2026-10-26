// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_THEME_PICKER_UW_THEME_PICKER_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_THEME_PICKER_UW_THEME_PICKER_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker.mojom.h"

namespace uw {

inline constexpr char kThemePickerHost[] = "uw-theme-picker.top-chrome";
inline constexpr char kThemePickerUrl[] =
    "chrome://uw-theme-picker.top-chrome/";

class UwThemePickerUI;

class UwThemePickerUIConfig
    : public DefaultTopChromeWebUIConfig<UwThemePickerUI> {
 public:
  UwThemePickerUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                    kThemePickerHost) {}

  // The bubble takes the page's size.
  bool ShouldAutoResizeHost() override;
};

// The theme picker's page, shown in a bubble beside the sidebar. It saves the
// profile's theme as the user picks.
class UwThemePickerUI : public TopChromeWebUIController,
                        public mojom::ThemePickerHandlerFactory {
 public:
  explicit UwThemePickerUI(content::WebUI* web_ui);
  UwThemePickerUI(const UwThemePickerUI&) = delete;
  UwThemePickerUI& operator=(const UwThemePickerUI&) = delete;
  ~UwThemePickerUI() override;

  static constexpr std::string_view GetWebUIName() { return "UwThemePicker"; }

  void BindInterface(
      mojo::PendingReceiver<mojom::ThemePickerHandlerFactory> receiver);

 private:
  class Handler;

  // mojom::ThemePickerHandlerFactory:
  void CreateHandler(
      mojo::PendingReceiver<mojom::ThemePickerHandler> handler) override;

  std::unique_ptr<Handler> handler_;
  mojo::Receiver<mojom::ThemePickerHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_THEME_PICKER_UW_THEME_PICKER_UI_H_
