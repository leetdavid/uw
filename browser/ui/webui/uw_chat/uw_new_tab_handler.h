// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class Profile;

namespace content {
class WebUI;
}  // namespace content

namespace uw {

// The new tab page's browser side: what its box asks, and the state of uw's
// features around it. See docs/plans/new-tab-page.md.
class UwNewTabHandler : public mojom::NewTabHandler {
 public:
  UwNewTabHandler(content::WebUI* web_ui,
                  mojo::PendingRemote<mojom::NewTabPage> page,
                  mojo::PendingReceiver<mojom::NewTabHandler> receiver);
  UwNewTabHandler(const UwNewTabHandler&) = delete;
  UwNewTabHandler& operator=(const UwNewTabHandler&) = delete;
  ~UwNewTabHandler() override;

  // mojom::NewTabHandler:
  void NewSchedule(NewScheduleCallback callback) override;
  void Ask(const std::string& text,
           const std::string& provider_id,
           const std::string& model) override;
  void AskOnSchedule(mojom::ChatSchedulePtr schedule) override;

 private:
  Profile* profile() const;
  void SendBox();
  // Shows `url` in this tab, as a chat that starts with `start` when given.
  void Become(const GURL& url, std::optional<ChatStart> start);

  const raw_ptr<content::WebUI> web_ui_;
  mojo::Remote<mojom::NewTabPage> page_;
  mojo::Receiver<mojom::NewTabHandler> receiver_;
  // Ask works once a provider serves the agent model.
  PrefChangeRegistrar prefs_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
