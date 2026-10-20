// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/containers/circular_deque.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/new_tab_grouping_user_data.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_dialog_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/grit/generated_resources.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/focused_node_details.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_ui.h"

namespace uw {
namespace {

constexpr int kTopInset = 96;
// A key the browser refused is tried again after about one frame.
constexpr base::TimeDelta kRetry = base::Milliseconds(16);
// The browser stops holding keys after this, even if the page never reported
// focus or kept refusing them.
constexpr base::TimeDelta kLongestHold = base::Seconds(1);

}  // namespace

// The command bar's page. Between the panel showing and its field taking
// focus, the page drops keys, so the browser holds them and hands them over in
// order once the field has focus. Until the page's first frame is on screen,
// the browser also refuses keys for it, so a refused key stays held and is
// tried again a frame later.
class CommandBar::Page : public WebUIContentsWrapperT<UwCommandBarUI>,
                         public content::RenderWidgetHost::InputEventObserver,
                         public CommandBarActions {
 public:
  Page(Profile* profile, CommandBar* bar)
      : WebUIContentsWrapperT(GURL(kCommandBarUrl),
                              profile,
                              IDS_UW_COMMAND_BAR),
        bar_(bar) {
    SetCommandBarActions(web_contents(), this);
  }

  void HoldKeys() {
    held_.clear();
    holding_ = true;
    give_up_at_ = base::TimeTicks::Now() + kLongestHold;
    next_send_.Start(FROM_HERE, kLongestHold,
                     base::BindOnce(&Page::SendKeys, base::Unretained(this)));
  }

  // WebUIContentsWrapper:
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    if (holding_ && !sending_ &&
        event.GetType() == input::NativeWebKeyboardEvent::Type::kRawKeyDown &&
        event.windows_key_code != ui::VKEY_ESCAPE) {
      held_.push_back(event);
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    return WebUIContentsWrapperT::PreHandleKeyboardEvent(source, event);
  }
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    // While held keys are sent, only a key the browser refused comes back
    // here. It stays held for the next try.
    return sending_ ||
           WebUIContentsWrapperT::HandleKeyboardEvent(source, event);
  }
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override {
    bar_->OpenPicked(params);
    return nullptr;
  }

  // CommandBarActions:
  void AskUw(const std::string& text) override { bar_->AskUw(text); }
  void RunInTerminal(const std::string& text) override {
    bar_->RunInTerminal(text);
  }
  void OnFocusChangedInPage(
      const content::FocusedNodeDetails& details) override {
    if (details.editable_level != content::EditableLevel::kNotEditable) {
      SendKeys();
    }
  }

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEventAck(const content::RenderWidgetHost& widget,
                       blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent& event) override {
    if (source == blink::mojom::InputEventResultSource::kBrowser &&
        state == blink::mojom::InputEventResultState::kNoConsumerExists) {
      refused_ = true;
    }
  }

 private:
  // Sends each held key down, and its character, as macOS would have, up to
  // the first key the browser refuses.
  void SendKeys() {
    if (!holding_) {
      return;
    }
    next_send_.Stop();
    if (web_contents()->GetVisibility() != content::Visibility::HIDDEN) {
      content::RenderWidgetHost* widget =
          web_contents()->GetPrimaryMainFrame()->GetRenderWidgetHost();
      base::AutoReset<bool> sending(&sending_, true);
      refused_ = false;
      widget->AddInputEventObserver(this);
      while (!held_.empty()) {
        input::NativeWebKeyboardEvent event = held_.front();
        widget->ForwardKeyboardEvent(event);
        if (refused_) {
          break;
        }
        held_.pop_front();
        if (event.text[0] >= ' ') {
          event.SetType(input::NativeWebKeyboardEvent::Type::kChar);
          event.skip_if_unhandled = true;
          widget->ForwardKeyboardEvent(event);
        }
      }
      widget->RemoveInputEventObserver(this);
      if (refused_ && base::TimeTicks::Now() < give_up_at_) {
        next_send_.Start(
            FROM_HERE, kRetry,
            base::BindOnce(&Page::SendKeys, base::Unretained(this)));
        return;
      }
    }
    held_.clear();
    holding_ = false;
  }

  const raw_ptr<CommandBar> bar_;
  bool holding_ = false;
  bool sending_ = false;
  bool refused_ = false;
  base::circular_deque<input::NativeWebKeyboardEvent> held_;
  base::TimeTicks give_up_at_;
  base::OneShotTimer next_send_;
};

DEFINE_USER_DATA(CommandBar);

CommandBar::CommandBar(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {
  setting_.Init(window->GetProfile()->GetPrefs());
  setting_.Add(kCommandBarEnabledPref,
               base::BindRepeating(&CommandBar::OnSettingChanged,
                                   base::Unretained(this)));
  OnSettingChanged();
}

CommandBar::~CommandBar() {
  if (bubble_) {
    bubble_->ClearContentsWrapper();
  }
}

CommandBar* CommandBar::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

bool CommandBar::Open(BrowserWindowInterface* window) {
  CommandBar* bar = From(window);
  if (!bar || !bar->contents_) {
    return false;
  }
  bar->Show();
  return true;
}

bool CommandBar::OpenForShellCommand(BrowserWindowInterface* window) {
  CommandBar* bar = From(window);
  if (!bar || !bar->contents_) {
    return false;
  }
  StartShellCommand(bar->page());
  bar->Show();
  return true;
}

bool CommandBar::IsShowing() const {
  return bubble_ && bubble_->GetWidget() && bubble_->GetWidget()->IsVisible();
}

content::WebContents* CommandBar::page() const {
  return contents_ ? contents_->web_contents() : nullptr;
}

void CommandBar::OnSettingChanged() {
  const bool on =
      window_->GetProfile()->GetPrefs()->GetBoolean(kCommandBarEnabledPref);
  if (on && !contents_) {
    contents_ = std::make_unique<Page>(window_->GetProfile(), this);
    webui::SetBrowserWindowInterface(contents_->web_contents(), window_);
  } else if (!on && contents_) {
    if (bubble_) {
      bubble_->GetWidget()->CloseNow();
    }
    contents_.reset();
  }
}

void CommandBar::Show() {
  if (IsShowing()) {
    return;
  }
  views::View* card =
      BrowserView::GetBrowserViewForBrowser(window_)->contents_container();
  const gfx::Rect area = card->GetBoundsInScreen();
  const gfx::Rect anchor(area.CenterPoint().x(), area.y() + kTopInset, 0, 0);
  // The page's last size is in pixels at the scale it was laid out at.
  content::RenderWidgetHostView* page_view = page()->GetRenderWidgetHostView();
  const float scale = page_view ? page_view->GetDeviceScaleFactor() : 0;
  auto view = std::make_unique<WebUIBubbleDialogView>(
      card, contents_->GetWeakPtr(), anchor, views::BubbleBorder::TOP_CENTER,
      /*autosize=*/true);
  bubble_ = view->GetWeakPtr();
  views::BubbleDialogDelegateView::CreateBubble(std::move(view));
  // Chromium sizes the panel while its new window is still on the main screen,
  // by that screen's scale. With a Retina main screen and this window on a 1x
  // display, the panel came up at half size and stayed so, since the page,
  // back at its own scale once over this window, reports no new size.
  const std::optional<gfx::Size>& size =
      page()->GetPrimaryMainFrame()->GetFrameSize();
  if (scale && size) {
    bubble_->ResizeDueToAutoResize(page(),
                                   gfx::ScaleToCeiledSize(*size, 1.f / scale));
  }
  contents_->HoldKeys();
  bubble_->ShowUI();
}

void CommandBar::OpenPicked(const content::OpenURLParams& params) {
  if (bubble_) {
    bubble_->CloseUI();
  }
  NavigateParams navigate(window_, params.url, params.transition);
  navigate.FillNavigateParamsFromOpenURLParams(params);
  navigate.source_contents = nullptr;
  navigate.tabstrip_index = -1;
  if (params.disposition == WindowOpenDisposition::SWITCH_TO_TAB) {
    navigate.disposition = WindowOpenDisposition::SWITCH_TO_TAB;
  } else {
    // As chrome::NewTab does, so the page lands where Cmd+T's blank tab
    // would have.
    navigate.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    const tabs::TabInterface* active =
        window_->GetTabStripModel()->GetActiveTab();
    window_->GetProfile()->SetUserData(
        NewTabGroupingUserData::kNewTabGroupingUserDataKey,
        std::make_unique<NewTabGroupingUserData>(active ? active->GetGroup()
                                                        : std::nullopt));
  }
  Navigate(&navigate);
}

void CommandBar::AskUw(const std::string& text) {
  tabs::TabInterface* asked_from = window_->GetTabStripModel()->GetActiveTab();
  if (bubble_) {
    bubble_->CloseUI();
  }
  AskInNewChat(window_, asked_from, text);
}

void CommandBar::RunInTerminal(const std::string& text) {
  if (bubble_) {
    bubble_->CloseUI();
  }
  OpenTerminalTabRunning(window_, text);
}

}  // namespace uw
