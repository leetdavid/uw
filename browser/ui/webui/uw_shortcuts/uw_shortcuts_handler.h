// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_SHORTCUTS_UW_SHORTCUTS_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_SHORTCUTS_UW_SHORTCUTS_HANDLER_H_

#include <string>

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "uw/browser/ui/webui/uw_shortcuts/uw_shortcuts.mojom.h"

namespace content {
class WebUIDataSource;
}

namespace uw {

// The Keyboard shortcuts section of chrome://settings: every command of the
// menu bar with its key, and changes to uw's keys. The keys are the app's, in
// local state.
class UwShortcutsHandler : public mojom::ShortcutsHandler {
 public:
  explicit UwShortcutsHandler(
      mojo::PendingReceiver<mojom::ShortcutsHandler> receiver);
  UwShortcutsHandler(const UwShortcutsHandler&) = delete;
  UwShortcutsHandler& operator=(const UwShortcutsHandler&) = delete;
  ~UwShortcutsHandler() override;

  static void AddLoadTimeData(content::WebUIDataSource* data_source);

  // mojom::ShortcutsHandler:
  void GetMenuKeys(GetMenuKeysCallback callback) override;
  void SetMenuKey(const std::string& name,
                  const std::string& key,
                  SetMenuKeyCallback callback) override;
  void ResetMenuKey(const std::string& name,
                    ResetMenuKeyCallback callback) override;
  void GiveBackMenuKey(const std::string& key,
                       GiveBackMenuKeyCallback callback) override;
  void ResetMenuKeys(ResetMenuKeysCallback callback) override;

 private:
  mojo::Receiver<mojom::ShortcutsHandler> receiver_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_SHORTCUTS_UW_SHORTCUTS_HANDLER_H_
