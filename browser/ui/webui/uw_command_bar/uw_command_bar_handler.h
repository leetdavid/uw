// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_HANDLER_H_

#include <optional>

#include "chrome/browser/ui/webui/searchbox/realbox_handler.h"

namespace uw {

// Chromium's searchbox handler, changed for the command bar: it asks for
// instant results only, opens a picked row in a new tab, switches to a row's
// open tab, and tells the page which kind of row each one is. Opened with Cmd
// it asks uw the typed text, and with Option it runs the text in a terminal.
class UwCommandBarHandler : public RealboxHandler {
 public:
  using RealboxHandler::RealboxHandler;
  UwCommandBarHandler(const UwCommandBarHandler&) = delete;
  UwCommandBarHandler& operator=(const UwCommandBarHandler&) = delete;
  ~UwCommandBarHandler() override;

  // Empties the page, and readies it for a shell command when `shell`, as a
  // typed ">" does.
  void Reset(bool shell);

  // searchbox::mojom::PageHandler:
  void OpenAutocompleteMatch(uint8_t line,
                             const GURL& url,
                             bool are_matches_showing,
                             uint8_t mouse_button,
                             searchbox::mojom::ActionModifiersPtr modifiers,
                             bool via_keyboard) override;

 protected:
  // SearchboxHandler:
  std::optional<searchbox::mojom::AutocompleteMatchPtr> CreateAutocompleteMatch(
      const AutocompleteMatch& match,
      size_t line,
      bookmarks::BookmarkModel* bookmark_model,
      const omnibox::GroupConfigMap& suggestion_groups_map,
      const TemplateURLService* turl_service) const override;
  WindowOpenDisposition ComputeWindowOpenDisposition(
      uint8_t mouse_button,
      bool alt_key,
      bool ctrl_key,
      bool meta_key,
      bool shift_key,
      bool via_keyboard) override;
  bool OmitsAsynchronousMatches() const override;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_UW_COMMAND_BAR_HANDLER_H_
