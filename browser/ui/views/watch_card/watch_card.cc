// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/watch_card/watch_card.h"

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/page_action/action_ids.h"
#include "chrome/browser/ui/page_action/page_action_controller.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_container_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/actions/actions.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_ui.h"

namespace uw {

DEFINE_USER_DATA(WatchCard);

WatchCard::WatchCard(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {
  window->GetTabStripModel()->AddObserver(this);
  if (ScheduledChats* chats = ScheduledChats::For(window->GetProfile())) {
    chats_observation_.Observe(chats);
  }
}

WatchCard::~WatchCard() = default;

// static
void WatchCard::AddAction(BrowserWindowInterface* window) {
  const std::u16string name = l10n_util::GetStringUTF16(IDS_UW_WATCH_THIS_PAGE);
  BrowserActions::From(window)->root_action_item()->AddChild(
      actions::ActionItem::Builder(
          base::BindRepeating(
              [](BrowserWindowInterface* window, actions::ActionItem* item,
                 actions::ActionInvocationContext context) { Open(window); },
              window))
          .SetActionId(page_actions::kActionUwWatchPage)
          .SetText(name)
          .SetTooltipText(name)
          .SetImage(ui::ImageModel::FromVectorIcon(
              vector_icons::kVisibilityIcon, ui::kColorIcon))
          .Build());
}

WatchCard* WatchCard::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

bool WatchCard::Open(BrowserWindowInterface* window) {
  WatchCard* card = From(window);
  tabs::TabInterface* tab = window ? window->GetActiveTabInterface() : nullptr;
  if (!card || !tab || !CanWatch(*tab)) {
    return false;
  }
  card->Show(*tab);
  return true;
}

bool WatchCard::CanWatch(const tabs::TabInterface& tab) {
  return tab.GetContents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

bool WatchCard::IsShowing() const {
  return bubble_ && bubble_->GetBubbleWidget() &&
         bubble_->GetBubbleWidget()->IsVisible();
}

content::WebContents* WatchCard::page() const {
  return bubble_ && bubble_->GetContentsWrapper()
             ? bubble_->GetContentsWrapper()->web_contents()
             : nullptr;
}

views::Widget* WatchCard::GetWidgetForTesting() const {
  return bubble_ ? bubble_->GetBubbleWidget() : nullptr;
}

void WatchCard::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted ||
      change.type() == TabStripModelChange::kReplaced) {
    UpdateEyes();
  }
}

void WatchCard::OnTabChangedAt(tabs::TabInterface* tab,
                               TabChangeType change_type) {
  if (change_type == TabChangeType::kAll) {
    UpdateEye(*tab);
  }
}

void WatchCard::OnScheduleChanged(const std::string& chat_id) {
  UpdateEyes();
}

void WatchCard::Show(tabs::TabInterface& tab) {
  if (bubble_ && bubble_->GetBubbleWidget()) {
    return;
  }
  views::View* eye = BrowserView::GetBrowserViewForBrowser(window_)
                         ->toolbar()
                         ->location_bar_view()
                         ->page_action_container()
                         ->GetPageActionView(page_actions::kActionUwWatchPage);
  if (!eye) {
    return;
  }
  // A new page each time, for the page of the tab it opens for.
  bubble_ = WebUIBubbleManager::Create<UwWatchCardUI>(
      window_, WatchCardUrl(tab), IDS_UW_WATCH_THIS_PAGE);
  bubble_->ShowBubble(eye, views::BubbleBorder::TOP_RIGHT);
  // The page frosts itself while the window's glass is on.
  FrostPageBubble(bubble_->GetBubbleWidget(),
                  bubble_->GetContentsWrapper()->web_contents());
}

void WatchCard::UpdateEye(tabs::TabInterface& tab) {
  page_actions::PageActionController* actions =
      tab.GetTabFeatures() ? tab.GetTabFeatures()->page_action_controller()
                           : nullptr;
  // A tab a scheduled chat's run opened, out of sight, has no eye.
  if (!actions || !actions->ActionExists(page_actions::kActionUwWatchPage)) {
    return;
  }
  if (!CanWatch(tab)) {
    actions->Hide(page_actions::kActionUwWatchPage);
    return;
  }
  const ScheduledChats* chats = chats_observation_.GetSource();
  const std::string* id =
      chats ? chats->ChatWatching(tab.GetContents()->GetLastCommittedURL())
            : nullptr;
  const ScheduledChat* chat = id ? chats->Find(*id) : nullptr;
  std::u16string name = l10n_util::GetStringUTF16(IDS_UW_WATCH_THIS_PAGE);
  if (chat && chat->watch_for.empty()) {
    name = l10n_util::GetStringUTF16(IDS_UW_WATCHING_THIS_PAGE);
  } else if (chat) {
    // "Price drops below $500" reads "Watching for price drops below $500".
    std::string watch_for = chat->watch_for;
    watch_for[0] = base::ToLowerASCII(watch_for[0]);
    name = l10n_util::GetStringFUTF16(IDS_UW_WATCHING_FOR,
                                      base::UTF8ToUTF16(watch_for));
  }
  actions->OverrideImage(
      page_actions::kActionUwWatchPage,
      ui::ImageModel::FromVectorIcon(chat ? kEyeFilledIcon
                                          : vector_icons::kVisibilityIcon),
      chat ? page_actions::PageActionColorSource::kCascadingAccent
           : page_actions::PageActionColorSource::kForeground);
  actions->OverrideTooltip(page_actions::kActionUwWatchPage, name);
  actions->OverrideAccessibleName(page_actions::kActionUwWatchPage, name);
  actions->Show(page_actions::kActionUwWatchPage);
}

void WatchCard::UpdateEyes() {
  TabStripModel* tabs = window_->GetTabStripModel();
  for (int i = 0; i < tabs->count(); ++i) {
    UpdateEye(*tabs->GetTabAtIndex(i));
  }
}

}  // namespace uw
