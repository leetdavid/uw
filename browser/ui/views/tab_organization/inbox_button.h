// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_INBOX_BUTTON_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_INBOX_BUTTON_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "uw/browser/agent/chat_alerts.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;
class WebUIBubbleManager;

namespace content {
class WebContents;
}  // namespace content

namespace uw {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kInboxButtonElementId);

// The Inbox button, at the right end of the line of window buttons at the top
// of the sidebar. A badge counts the alerts the user has not seen, and a press
// opens the Inbox under it and sees them all. See docs/product.md, "Scheduled
// chats".
class InboxButton : public views::Button,
                    public ChatAlerts::Observer,
                    public views::WidgetObserver {
  METADATA_HEADER(InboxButton, views::Button)

 public:
  // The button of `window`, or null in a private window, which keeps no
  // alerts.
  static std::unique_ptr<InboxButton> Make(BrowserWindowInterface* window);
  // Opens the Inbox of `window`, as a press on its button does. While the
  // sidebar is hidden, it stays hidden, and the Inbox shows by the page's
  // top-left corner.
  static void OpenIn(BrowserWindowInterface* window);

  InboxButton(BrowserWindowInterface* window, ChatAlerts* alerts);
  InboxButton(const InboxButton&) = delete;
  InboxButton& operator=(const InboxButton&) = delete;
  ~InboxButton() override;

  bool IsShowing() const;
  content::WebContents* page() const;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // ChatAlerts::Observer:
  void OnAlertsChanged() override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void Open();
  void Show(bool by_page_corner);
  void UpdateName();

  const raw_ptr<BrowserWindowInterface> window_;
  base::ScopedObservation<ChatAlerts, ChatAlerts::Observer> alerts_{this};
  std::unique_ptr<WebUIBubbleManager> bubble_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> bubble_widget_{
      this};
  // Keeps the floating sidebar out while the Inbox is open.
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  base::CallbackListSubscription paint_as_active_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_INBOX_BUTTON_H_
