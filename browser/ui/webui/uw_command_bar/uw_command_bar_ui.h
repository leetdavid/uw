// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_UI_H_
#define UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_UI_H_

#include <memory>
#include <string>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "components/omnibox/browser/searchbox.mojom.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace content {
class WebContents;
}

namespace uw {

class UwCommandBarHandler;

inline constexpr char kCommandBarHost[] = "uw-command-bar.top-chrome";
inline constexpr char kCommandBarUrl[] = "chrome://uw-command-bar.top-chrome/";

// What the command bar's page asks its window to do with the typed text.
class CommandBarActions {
 public:
  // Cmd+Return: asks uw in a new chat.
  virtual void AskUw(const std::string& text) = 0;
  // Option+Return, or Return after a ">": runs it in a new terminal.
  virtual void RunInTerminal(const std::string& text) = 0;

 protected:
  virtual ~CommandBarActions() = default;
};

// Gives the command bar's page in `page` its window's actions.
void SetCommandBarActions(content::WebContents* page,
                          CommandBarActions* actions);
// The actions of the command bar's page in `page`, or null.
CommandBarActions* GetCommandBarActions(content::WebContents* page);

// Readies the command bar's page in `page` for a shell command, as a typed
// ">" does.
void StartShellCommand(content::WebContents* page);

class UwCommandBarUI;

class UwCommandBarUIConfig
    : public DefaultTopChromeWebUIConfig<UwCommandBarUI> {
 public:
  UwCommandBarUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, kCommandBarHost) {
  }

  // TopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
};

// The command bar's page. Its suggestions come from the searchbox handler
// behind the new tab page's search box.
class UwCommandBarUI : public TopChromeWebUIController,
                       public searchbox::mojom::PageHandlerFactory {
 public:
  explicit UwCommandBarUI(content::WebUI* web_ui);
  UwCommandBarUI(const UwCommandBarUI&) = delete;
  UwCommandBarUI& operator=(const UwCommandBarUI&) = delete;
  ~UwCommandBarUI() override;

  static constexpr std::string_view GetWebUIName() { return "UwCommandBar"; }

  void BindInterface(
      mojo::PendingReceiver<searchbox::mojom::PageHandlerFactory> receiver);

 private:
  friend void StartShellCommand(content::WebContents* page);

  // searchbox::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<searchbox::mojom::Page> page,
      mojo::PendingReceiver<searchbox::mojom::PageHandler> handler) override;

  std::unique_ptr<UwCommandBarHandler> handler_;
  mojo::Receiver<searchbox::mojom::PageHandlerFactory> factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_UI_H_
