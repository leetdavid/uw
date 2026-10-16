// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_handler.h"

#include <utility>

#include "base/time/time.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/omnibox/browser/actions/omnibox_action.h"
#include "components/omnibox/browser/autocomplete_match.h"

namespace uw {
namespace {

// The index of the row's "switch to this tab" action, if it has one.
std::optional<uint8_t> TabSwitchAction(const AutocompleteMatch& match) {
  for (size_t i = 0; i < match.actions.size(); ++i) {
    if (match.actions[i]->ActionId() == OmniboxActionId::TAB_SWITCH) {
      return static_cast<uint8_t>(i);
    }
  }
  return std::nullopt;
}

// The label on the row's right in the command bar.
std::string Kind(const AutocompleteMatch& match,
                 const bookmarks::BookmarkModel* bookmark_model) {
  if (TabSwitchAction(match) || match.has_tab_match.value_or(false)) {
    return "tab";
  }
  if (match.type == AutocompleteMatchType::URL_WHAT_YOU_TYPED) {
    return "";
  }
  if (AutocompleteMatch::IsSearchType(match.type)) {
    return "search";
  }
  if (match.type == AutocompleteMatchType::BOOKMARK_TITLE ||
      (bookmark_model && bookmark_model->IsBookmarked(match.destination_url))) {
    return "bookmark";
  }
  return "history";
}

}  // namespace

UwCommandBarHandler::~UwCommandBarHandler() = default;

void UwCommandBarHandler::OpenAutocompleteMatch(
    uint8_t line,
    const GURL& url,
    bool are_matches_showing,
    uint8_t mouse_button,
    searchbox::mojom::ActionModifiersPtr modifiers,
    bool via_keyboard) {
  if (const AutocompleteMatch* match = GetMatchWithUrl(line, url)) {
    if (const std::optional<uint8_t> action = TabSwitchAction(*match)) {
      ExecuteAction(line, *action, url, base::TimeTicks::Now(), mouse_button,
                    /*alt_key=*/false, /*ctrl_key=*/false, /*meta_key=*/false,
                    /*shift_key=*/false);
      return;
    }
  }
  RealboxHandler::OpenAutocompleteMatch(line, url, are_matches_showing,
                                        mouse_button, std::move(modifiers),
                                        via_keyboard);
}

std::optional<searchbox::mojom::AutocompleteMatchPtr>
UwCommandBarHandler::CreateAutocompleteMatch(
    const AutocompleteMatch& match,
    size_t line,
    bookmarks::BookmarkModel* bookmark_model,
    const omnibox::GroupConfigMap& suggestion_groups_map,
    const TemplateURLService* turl_service) const {
  std::optional<searchbox::mojom::AutocompleteMatchPtr> row =
      RealboxHandler::CreateAutocompleteMatch(
          match, line, bookmark_model, suggestion_groups_map, turl_service);
  if (row) {
    // The page reads `type` as the row's kind.
    (*row)->type = Kind(match, bookmark_model);
  }
  return row;
}

WindowOpenDisposition UwCommandBarHandler::ComputeWindowOpenDisposition(
    uint8_t mouse_button,
    bool alt_key,
    bool ctrl_key,
    bool meta_key,
    bool shift_key,
    bool via_keyboard) {
  return WindowOpenDisposition::NEW_FOREGROUND_TAB;
}

bool UwCommandBarHandler::OmitsAsynchronousMatches() const {
  return true;
}

}  // namespace uw
