// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/containers/circular_deque.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/new_tab_grouping_user_data.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_dialog_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/grit/generated_resources.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/focused_node_details.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/little_window/little_window_mark.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar_ui.h"

namespace uw {
namespace {

constexpr int kTopInset = 96;
// In a little window, which is small, it sits closer under the toolbar.
constexpr int kLittleWindowTopInset = 48;
// A key the browser refused is tried again after about one frame.
constexpr base::TimeDelta kRetry = base::Milliseconds(16);
// The browser stops holding keys after this, even if the page never reported
// focus or kept refusing them.
constexpr base::TimeDelta kLongestHold = base::Seconds(1);

// Closes little window `window` when it never showed a page, as Esc in its
// command bar does before anything is picked.
void CloseLittleWindowIfBlank(BrowserWindowInterface* window) {
  tabs::TabInterface* tab = window->GetTabStripModel()->GetActiveTab();
  if (!IsLittleWindow(window) || !tab) {
    return;
  }
  const GURL shown = tab->GetContents()->GetLastCommittedURL();
  if (tab->GetContents()->GetController().GetPendingEntry() ||
      !(shown.is_empty() || shown.IsAboutBlank())) {
    return;
  }
  window->GetWindow()->Close();
}

CommandBar::CommandSource& Source() {
  static base::NoDestructor<CommandBar::CommandSource> source;
  return *source;
}

// The workspace's tasks in sidebar order, then the Scratchpad, each with the
// addresses its tabs in `window` show.
mojom::BarTasksPtr ListTasks(BrowserWindowInterface* window, bool moving) {
  auto list = mojom::BarTasks::New();
  list->moving = moving;
  const TabOrganizationController* controller =
      TabOrganizationController::From(window);
  const TabGroupModel* groups = window->GetTabStripModel()->group_model();
  if (!controller || !groups) {
    return list;
  }
  const ui::ColorProvider* colors =
      BrowserView::GetBrowserViewForBrowser(window)->GetColorProvider();
  std::vector<std::string> tasks = controller->tree().Tasks();
  tasks.emplace_back();
  for (const std::string& key : tasks) {
    auto task = mojom::BarTask::New();
    task->id = key;
    if (key.empty()) {
      task->name = l10n_util::GetStringUTF8(IDS_UW_SCRATCHPAD);
    } else {
      const std::optional<tab_groups::TabGroupId> group =
          controller->GroupFor(key);
      if (!group || !groups->ContainsTabGroup(*group)) {
        continue;
      }
      task->name = base::UTF16ToUTF8(controller->TaskTitle(key));
      task->color = color_utils::SkColorToRgbaString(
          colors->GetColor(GetTabGroupTabStripColorId(
              groups->GetTabGroup(*group)->visual_data()->color(),
              /*active_frame=*/true)));
    }
    for (const Page& page : controller->tree().pages()) {
      if (page.task != key) {
        continue;
      }
      ++task->tab_count;
      if (const tabs::TabInterface* tab = controller->TabFor(page.id)) {
        task->urls.push_back(
            tab->GetContents()->GetLastCommittedURL().spec());
      }
    }
    list->tasks.push_back(std::move(task));
  }
  const tabs::TabInterface* selected = window->GetActiveTabInterface();
  if (selected && selected->GetGroup()) {
    list->selected = controller->KeyOf(*selected->GetGroup());
  }
  return list;
}

}  // namespace

// The command bar's page. Between the panel showing and its field taking
// focus, the page drops keys, so the browser holds them, Escape too, and hands
// them over in order once the field has focus. The page handles Escape itself:
// it empties the field first, and closes the panel once the field is empty.
// Until the page's first frame is on screen, the browser also refuses keys for
// it, so a refused key stays held and is tried again a frame later.
class CommandBar::Page : public WebUIContentsWrapperT<UwCommandBarUI>,
                         public content::RenderWidgetHost::InputEventObserver,
                         public CommandBarActions {
 public:
  Page(Profile* profile, CommandBar* bar)
      : WebUIContentsWrapperT(GURL(kCommandBarUrl),
                              profile,
                              IDS_UW_COMMAND_BAR,
                              /*esc_closes_ui=*/false),
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
        event.GetType() == input::NativeWebKeyboardEvent::Type::kRawKeyDown) {
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
  void AskUw(const std::string& text) override { bar_->Ask(text, nullptr); }
  void AskAboutPage(const std::string& text) override {
    bar_->Ask(text, bar_->SelectedPage());
  }
  void RunInTerminal(const std::string& text) override {
    bar_->RunInTerminal(text);
  }
  content::WebContents* SelectedPage() override {
    tabs::TabInterface* page = bar_->SelectedPage();
    return page ? page->GetContents() : nullptr;
  }
  std::vector<mojom::BarCommandPtr> Commands() override {
    std::vector<mojom::BarCommandPtr> listed;
    if (!Source()) {
      return listed;
    }
    for (const Command& command : Source().Run(bar_->window_)) {
      std::vector<std::string> keys;
      for (const std::u16string& key : command.keys) {
        keys.push_back(base::UTF16ToUTF8(key));
      }
      listed.push_back(mojom::BarCommand::New(
          command.id, base::UTF16ToUTF8(command.name), std::move(keys)));
    }
    return listed;
  }
  mojom::BarTasksPtr Tasks() override {
    return ListTasks(bar_->window_, bar_->moving_);
  }
  void RunCommand(int command) override { bar_->RunCommand(command); }
  void GoToTask(const std::string& task) override { bar_->GoToTask(task); }
  void MoveToTask(const std::string& task) override {
    bar_->MoveToTask(task);
  }
  void MoveToNewTask() override {
    bar_->CloseForPick();
    chrome::ExecuteCommand(bar_->window_, TabOrganizationController::kNewTask);
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
  // A new window on a workspace with tabs opens with the command bar over
  // its blank tab.
  if (TabOrganizationController* tabs =
          TabOrganizationController::From(window)) {
    requests_ = tabs->ObserveCommandBarRequests(base::BindRepeating(
        [](BrowserWindowInterface* window) { CommandBar::Open(window); },
        window));
  }
}

CommandBar::~CommandBar() {
  if (bubble_) {
    bubble_->ClearContentsWrapper();
  }
}

void CommandBar::SetCommandSource(CommandSource source) {
  Source() = std::move(source);
}

CommandBar* CommandBar::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

bool CommandBar::Open(BrowserWindowInterface* window) {
  return OpenWith(window, std::nullopt, nullptr, /*moving=*/false,
                  /*reset_shell=*/std::nullopt);
}

bool CommandBar::OpenForShellCommand(BrowserWindowInterface* window) {
  return OpenWith(window, std::nullopt, nullptr, /*moving=*/false,
                  /*reset_shell=*/true);
}

bool CommandBar::OpenForTask(BrowserWindowInterface* window,
                             const tab_groups::TabGroupId& task) {
  return OpenWith(window, task, nullptr, /*moving=*/false,
                  /*reset_shell=*/std::nullopt);
}

bool CommandBar::OpenForPane(BrowserWindowInterface* window,
                             tabs::TabInterface* pane) {
  return OpenWith(window, std::nullopt, pane, /*moving=*/false,
                  /*reset_shell=*/std::nullopt);
}

bool CommandBar::OpenToMoveTabs(BrowserWindowInterface* window) {
  // The page asks for the tasks as it empties.
  return OpenWith(window, std::nullopt, nullptr, /*moving=*/true,
                  /*reset_shell=*/false);
}

bool CommandBar::OpenWith(BrowserWindowInterface* window,
                          std::optional<tab_groups::TabGroupId> task,
                          tabs::TabInterface* pane,
                          bool moving,
                          std::optional<bool> reset_shell) {
  CommandBar* bar = From(window);
  if (!bar || !bar->contents_) {
    return false;
  }
  bar->task_ = task;
  bar->pane_ = pane ? pane->GetHandle() : tabs::TabHandle();
  bar->moving_ = moving;
  if (reset_shell) {
    ResetCommandBar(bar->page(), *reset_shell);
  }
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
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(window_);
  views::View* card = browser_view->contents_container();
  gfx::Rect area = card->GetBoundsInScreen();
  // Opened for a new pane, it shows over that pane, where the split puts it
  // even before the page area is laid out again.
  if (tabs::TabInterface* pane = pane_.Get(); pane && pane->GetSplit()) {
    const split_tabs::SplitTabData* split =
        window_->GetTabStripModel()->GetSplitData(*pane->GetSplit());
    const std::vector<tabs::TabInterface*> tabs = split->ListTabs();
    MultiContentsView* page_area = browser_view->multi_contents_view();
    const std::vector<gfx::Rect> panes =
        page_area->GetPaneBounds(split->visual_data()->panes());
    const size_t index = std::ranges::find(tabs, pane) - tabs.begin();
    if (index < panes.size()) {
      area = panes[index] + page_area->GetBoundsInScreen().OffsetFromOrigin();
    }
  }
  const gfx::Rect anchor(
      area.CenterPoint().x(),
      area.y() + (IsLittleWindow(window_) ? kLittleWindowTopInset : kTopInset),
      0, 0);
  // The page's last size is in pixels at the scale it was laid out at.
  content::RenderWidgetHostView* page_view = page()->GetRenderWidgetHostView();
  const float scale = page_view ? page_view->GetDeviceScaleFactor() : 0;
  auto view = std::make_unique<WebUIBubbleDialogView>(
      card, contents_->GetWeakPtr(), anchor, views::BubbleBorder::TOP_CENTER,
      /*autosize=*/true);
  bubble_ = view->GetWeakPtr();
  panel_.Observe(
      views::BubbleDialogDelegateView::CreateBubble(std::move(view)));
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
  // The page asks, as it shows, whether to frost itself.
  SetCommandBarFrosted(page(), FrostsOver(window_));
  FrostPageBubble(bubble_->GetWidget(), page());
  contents_->HoldKeys();
  bubble_->ShowUI();
}

void CommandBar::OnWidgetClosing(views::Widget* widget) {
  panel_.Reset();
  if (widget->closed_reason() != views::Widget::ClosedReason::kLostFocus) {
    moving_ = false;
    ResetCommandBar(page(), /*shell=*/false);
    // Esc before anything was picked closes an empty little window.
    if (!opening_) {
      CloseLittleWindowIfBlank(window_);
    }
  }
}

void CommandBar::OnWidgetDestroying(views::Widget* widget) {
  panel_.Reset();
}

void CommandBar::OpenPicked(const content::OpenURLParams& params) {
  CloseForPick();
  NavigateParams navigate(window_, params.url, params.transition);
  navigate.FillNavigateParamsFromOpenURLParams(params);
  navigate.source_contents = nullptr;
  navigate.tabstrip_index = -1;
  if (params.disposition == WindowOpenDisposition::SWITCH_TO_TAB) {
    navigate.disposition = WindowOpenDisposition::SWITCH_TO_TAB;
  } else if (IsLittleWindow(window_)) {
    // A little window shows what was picked in itself.
    navigate.disposition = WindowOpenDisposition::CURRENT_TAB;
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
  // Over a new pane, an open tab of this window moves into the pane. Navigate
  // would switch to it, and close the pane's blank tab on the way.
  if (params.disposition == WindowOpenDisposition::SWITCH_TO_TAB &&
      pane_.Get()) {
    TabStripModel* model = window_->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (tab->GetContents()->GetLastCommittedURL() == params.url) {
        PlaceInPane(tab);
        return;
      }
    }
  }
  Navigate(&navigate);
  if (navigate.disposition != WindowOpenDisposition::SWITCH_TO_TAB) {
    PlaceInTask();
  }
  PlaceInPane(window_->GetTabStripModel()->GetActiveTab());
  // An open tab picked in an empty little window takes its place.
  CloseLittleWindowIfBlank(window_);
}

tabs::TabInterface* CommandBar::SelectedPage() {
  tabs::TabInterface* tab = window_->GetTabStripModel()->GetActiveTab();
  return tab && tab->GetContents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()
             ? tab
             : nullptr;
}

void CommandBar::Ask(const std::string& text, tabs::TabInterface* page) {
  CloseForPick();
  AskInNewChat(window_, page, text);
  PlaceInTask();
  PlaceInPane(window_->GetTabStripModel()->GetActiveTab());
  CloseLittleWindowIfBlank(window_);
}

void CommandBar::RunInTerminal(const std::string& text) {
  CloseForPick();
  OpenTerminalTabRunning(window_, text);
  PlaceInTask();
  PlaceInPane(window_->GetTabStripModel()->GetActiveTab());
}

void CommandBar::RunCommand(int command) {
  std::vector<Command> listed;
  if (Source()) {
    listed = Source().Run(window_);
  }
  if (!std::ranges::contains(listed, command, &Command::id)) {
    return;
  }
  // Move to Task… lists the tasks in the command bar, which stays.
  if (command != kMoveToTask) {
    CloseForPick();
  }
  chrome::ExecuteCommand(window_, command);
}

void CommandBar::GoToTask(const std::string& task) {
  CloseForPick();
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    controller->SelectTask(task);
  }
}

void CommandBar::MoveToTask(const std::string& task) {
  CloseForPick();
  TabOrganizationController* controller =
      TabOrganizationController::From(window_);
  if (!controller) {
    return;
  }
  if (task.empty()) {
    controller->MoveToScratchpad(controller->SelectionFor());
  } else if (const std::optional<tab_groups::TabGroupId> group =
                 controller->GroupFor(task)) {
    controller->MoveToTask(controller->SelectionFor(), *group);
  }
}

void CommandBar::CloseForPick() {
  if (bubble_) {
    base::AutoReset<bool> opening(&opening_, true);
    bubble_->CloseUI();
  }
}

void CommandBar::PlaceInTask() {
  const std::optional<tab_groups::TabGroupId> task = std::exchange(task_, {});
  TabOrganizationController* controller =
      TabOrganizationController::From(window_);
  if (task && controller) {
    controller->PlaceInTask(window_->GetTabStripModel()->GetActiveTab(), *task);
  }
}

void CommandBar::PlaceInPane(tabs::TabInterface* tab) {
  tabs::TabInterface* pane = std::exchange(pane_, tabs::TabHandle()).Get();
  TabStripModel* model = window_->GetTabStripModel();
  // A tab picked in another window, or already in this split, stays put.
  if (!pane || !pane->IsSplit() || !tab || tab == pane || tab->IsPinned() ||
      tab->GetSplit() == pane->GetSplit()) {
    return;
  }
  if (tab->IsSplit()) {
    model->MoveTabOutOfSplit(model->GetIndexOfTab(tab));
  }
  // The split takes another tab only while one of its own is selected.
  model->ActivateTabAt(model->GetIndexOfTab(pane));
  model->UpdateTabInSplit(pane, model->GetIndexOfTab(tab),
                          TabStripModel::SplitUpdateType::kReplace);
}

}  // namespace uw
