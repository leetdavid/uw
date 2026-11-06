// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/key_card.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/sessions/core/session_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/floating_sidebar.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/tab_organization/modifier_keys.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

using Controller = TabOrganizationController;

// How long a scope's keys are held before the card shows. A key pressed
// sooner never shows it.
constexpr base::TimeDelta kDelay = base::Milliseconds(500);

// The card sits this far from the sidebar and the toolbar.
constexpr int kInset = 16;
constexpr int kCardWidth = 340;
constexpr int kRowHeight = 28;
constexpr int kRowInset = 6;
// The lane of key caps, so the names line up.
constexpr int kKeysWidth = 52;
constexpr int kCapHeight = 22;
constexpr int kCapMinWidth = 22;
constexpr int kCapInset = 6;
constexpr int kCapRadius = 6;
constexpr int kCapGap = 3;
constexpr int kGap = 10;
constexpr int kIconSize = 14;
// A command that can't run now.
constexpr float kDimmed = 0.38f;
// The tint of the sidebar rows the keys act on.
constexpr SkAlpha kTintAlpha = 0x24;

base::TimeDelta& Delay() {
  static base::TimeDelta delay = kDelay;
  return delay;
}

// The scope whose two keys, and no others, are held.
KeyScope HeldScope() {
  const ModifierKeys& keys = ModifierKeys::Get();
#if BUILDFLAG(IS_MAC)
  if (keys.command() && !keys.shift()) {
    if (keys.option() && !keys.control()) {
      return KeyScope::kBranch;
    }
    if (keys.control() && !keys.option()) {
      return KeyScope::kTask;
    }
  }
#else
  // Option is Alt and Cmd is Ctrl here.
  if (keys.option() && keys.command() && !keys.shift()) {
    return KeyScope::kBranch;
  }
  if (keys.option() && keys.shift() && !keys.command()) {
    return KeyScope::kTask;
  }
#endif
  return KeyScope::kNone;
}

int ScopeModifiers(KeyScope scope) {
#if BUILDFLAG(IS_MAC)
  return scope == KeyScope::kBranch ? ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN
                                    : ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN;
#else
  return scope == KeyScope::kBranch ? ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN
                                    : ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN;
#endif
}

// The held keys, as the card's header names them.
std::vector<std::u16string> ScopeKeyNames(KeyScope scope) {
#if BUILDFLAG(IS_MAC)
  const std::u16string cmd = l10n_util::GetStringUTF16(IDS_UW_KEY_CARD_CMD);
  return scope == KeyScope::kBranch
             ? std::vector<std::u16string>{l10n_util::GetStringUTF16(
                                               IDS_UW_KEY_CARD_OPTION),
                                           cmd}
             : std::vector<std::u16string>{
                   l10n_util::GetStringUTF16(IDS_APP_CTRL_KEY), cmd};
#else
  const std::u16string alt = l10n_util::GetStringUTF16(IDS_APP_ALT_KEY);
  return scope == KeyScope::kBranch
             ? std::vector<std::u16string>{l10n_util::GetStringUTF16(
                                               IDS_APP_CTRL_KEY),
                                           alt}
             : std::vector<std::u16string>{
                   alt, l10n_util::GetStringUTF16(IDS_APP_SHIFT_KEY)};
#endif
}

// What a key cap says for `key`.
std::u16string KeyName(ui::KeyboardCode key) {
  if ((key >= ui::VKEY_A && key <= ui::VKEY_Z) ||
      (key >= ui::VKEY_0 && key <= ui::VKEY_9)) {
    return std::u16string(1, static_cast<char16_t>(key));
  }
  switch (key) {
    case ui::VKEY_OEM_PERIOD:
      return u".";
    case ui::VKEY_OEM_4:
      return u"[";
    case ui::VKEY_OEM_6:
      return u"]";
    case ui::VKEY_UP:
      return u"\u2191";
    case ui::VKEY_DOWN:
      return u"\u2193";
    case ui::VKEY_LEFT:
      return u"\u2190";
    case ui::VKEY_RIGHT:
      return u"\u2192";
    case ui::VKEY_RETURN:
#if BUILDFLAG(IS_MAC)
      return l10n_util::GetStringUTF16(IDS_UW_KEY_CARD_RETURN);
#else
      return l10n_util::GetStringUTF16(IDS_APP_ENTER_KEY);
#endif
    default:
      return ui::Accelerator(key, ui::EF_NONE).GetShortcutText();
  }
}

// `command`'s key in `window`, when it is one of `scope`'s keys.
std::optional<ui::Accelerator> KeyIn(BrowserWindowInterface* window,
                                     int command,
                                     KeyScope scope) {
  ui::Accelerator key;
  if (!GetCommandAccelerator(window, command, &key)) {
#if BUILDFLAG(IS_MAC)
    // Chromium binds these outside the menu bar, where no lookup finds them.
    constexpr int kCtrlCmd = ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN;
    if (command == IDC_FOCUS_NEXT_TAB_GROUP) {
      key = ui::Accelerator(ui::VKEY_X, kCtrlCmd);
    } else if (command == IDC_FOCUS_PREV_TAB_GROUP) {
      key = ui::Accelerator(ui::VKEY_Z, kCtrlCmd);
    } else {
      return std::nullopt;
    }
#else
    return std::nullopt;
#endif
  }
  if (key.modifiers() != ScopeModifiers(scope)) {
    return std::nullopt;
  }
  return key;
}

bool CanRun(BrowserWindowInterface* window, int command) {
  if (IsWindowCommand(command)) {
    return CanRunWindowCommand(window, command);
  }
  const tabs::TabInterface* tab = window->GetActiveTabInterface();
  const TabGroupModel* groups = window->GetTabStripModel()->group_model();
  switch (command) {
    case IDC_CLOSE_TAB_GROUP:
      return tab && tab->GetGroup().has_value();
    case IDC_FOCUS_NEXT_TAB_GROUP:
    case IDC_FOCUS_PREV_TAB_GROUP:
      return groups && !groups->ListTabGroups().empty();
    default:
      return true;
  }
}

std::unique_ptr<views::Label> MakeCap(const std::u16string& text, bool held) {
  auto cap = std::make_unique<views::Label>(text);
  cap->SetFontList(cap->font_list().DeriveWithSizeDelta(-2).DeriveWithWeight(
      gfx::Font::Weight::MEDIUM));
  cap->SetAutoColorReadabilityEnabled(false);
  cap->SetSubpixelRenderingEnabled(false);
  cap->SetEnabledColor(held ? kColorMenuCardBackground : kColorMenuCardText);
  cap->SetBackground(views::CreateRoundedRectBackground(
      held ? kColorMenuCardText : kColorMenuCardHover, kCapRadius));
  cap->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kCapInset)));
  cap->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  cap->SetPreferredSize(gfx::Size(
      std::max(kCapMinWidth, cap->GetPreferredSize({}).width()), kCapHeight));
  return cap;
}

std::unique_ptr<views::Label> MakeText(const std::u16string& text,
                                       ui::ColorId color) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(color);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

void Shrinks(views::View* view) {
  view->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred));
}

// One key, or a pair, and what it does.
std::unique_ptr<views::View> MakeRow(const std::vector<std::u16string>& keys,
                                     const std::u16string& name,
                                     bool dimmed) {
  auto row = std::make_unique<views::FlexLayoutView>();
  row->SetOrientation(views::LayoutOrientation::kHorizontal);
  row->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  row->SetInteriorMargin(gfx::Insets::VH(0, kRowInset));
  row->SetMinimumCrossAxisSize(kRowHeight);
  auto* lane = row->AddChildView(std::make_unique<views::BoxLayoutView>());
  lane->SetBetweenChildSpacing(kCapGap);
  lane->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  lane->SetPreferredSize(gfx::Size(kKeysWidth, kCapHeight));
  for (const std::u16string& key : keys) {
    lane->AddChildView(MakeCap(key, /*held=*/false));
  }
  views::Label* label = row->AddChildView(MakeText(name, kColorMenuCardText));
  label->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, kGap, 0, 0));
  Shrinks(label);
  if (dimmed) {
    row->SetPaintToLayer();
    row->layer()->SetFillsBoundsOpaquely(false);
    row->layer()->SetOpacity(kDimmed);
  }
  return row;
}

std::unique_ptr<views::Label> MakeSectionLabel(const std::u16string& text) {
  auto label = MakeText(text, kColorMenuCardMuted);
  label->SetFontList(
      label->font_list().DeriveWithSizeDelta(-2).DeriveWithWeight(
          gfx::Font::Weight::SEMIBOLD));
  label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(8, kRowInset, 2, kRowInset)));
  return label;
}

// The rows a scope's card can list, in order. A row with a second command
// shows both keys, such as [ and ].
struct RowSpec {
  int command;
  int second = 0;
  int name;
};

constexpr RowSpec kBranchRows[] = {
    {Controller::kMoveToPreviousTask, Controller::kMoveToNextTask,
     IDS_UW_KEY_CARD_PREVIOUS_NEXT_TASK},
    {Controller::kScratchpad, 0, IDS_UW_MOVE_TO_SCRATCHPAD_MAC},
    {Controller::kNewChildTab, 0, IDS_UW_NEW_CHILD_TAB_MAC},
    {Controller::kKeepHere, 0, IDS_UW_KEEP_HERE_MAC},
    {Controller::kToggleBranch, 0, IDS_UW_COLLAPSE_BRANCH_MAC},
    {Controller::kCloseBranch, 0, IDS_UW_CLOSE_BRANCH_MAC},
};
constexpr RowSpec kBranchAlsoRows[] = {
    {Controller::kUndo, 0, IDS_UW_UNDO_ORGANIZATION_MAC},
};
constexpr RowSpec kTaskRows[] = {
    {IDC_CREATE_NEW_TAB_GROUP, 0, IDS_UW_NEW_TASK_MAC},
    {IDC_CLOSE_TAB_GROUP, 0, IDS_UW_CLOSE_TASK_MAC},
    {kRenameTask, 0, IDS_UW_RENAME_TASK_MAC},
    {IDC_FOCUS_NEXT_TAB_GROUP, IDC_FOCUS_PREV_TAB_GROUP,
     IDS_UW_KEY_CARD_NEXT_PREVIOUS_TASK},
    {kGoToTask1, kGoToTask9, IDS_UW_GO_TO_TASK_MAC},
    {kGoToScratchpad, 0, IDS_UW_KEY_CARD_GO_TO_SCRATCHPAD},
    {kConfirmSuggestion, 0, IDS_UW_CONFIRM_SUGGESTION_MAC},
};
constexpr RowSpec kArrowRows[] = {
    {Controller::kMoveUp, Controller::kMoveDown, IDS_UW_KEY_CARD_MOVE_UP_DOWN},
    {Controller::kNest, Controller::kOutdent, IDS_UW_KEY_CARD_NEST_OUTDENT},
};

// The keys of `scope` over the window in front, and what they act on.
class KeyCardView : public views::View {
  METADATA_HEADER(KeyCardView, views::View)

 public:
  KeyCardView(BrowserWindowInterface* window, KeyScope scope)
      : window_(window), scope_(scope) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    AddHeader();
    AddSubject();
    AddChildView(MakeMenuCardSeparator());
    const bool list_tasks = scope == KeyScope::kTask && IsSidebarHidden(window);
    AddRows(scope == KeyScope::kBranch ? base::span<const RowSpec>(kBranchRows)
                                       : base::span<const RowSpec>(kTaskRows),
            list_tasks);
    if (list_tasks) {
      AddTasks();
    }
    if (scope == KeyScope::kBranch) {
      AddSection(IDS_UW_KEY_CARD_ALSO, kBranchAlsoRows);
    } else {
      AddSection(IDS_UW_KEY_CARD_ARROWS, kArrowRows);
    }
  }

 private:
  Controller* controller() { return Controller::From(window_); }

  void AddHeader() {
    auto* header = AddChildView(std::make_unique<views::FlexLayoutView>());
    header->SetOrientation(views::LayoutOrientation::kHorizontal);
    header->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
    header->SetInteriorMargin(gfx::Insets::TLBR(2, kRowInset, 0, kRowInset));
    for (const std::u16string& name : ScopeKeyNames(scope_)) {
      header->AddChildView(MakeCap(name, /*held=*/true))
          ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, 6));
    }
    views::Label* title = header->AddChildView(
        MakeText(l10n_util::GetStringUTF16(scope_ == KeyScope::kBranch
                                               ? IDS_UW_KEY_CARD_BRANCH
                                               : IDS_UW_KEY_CARD_TASK),
                 kColorMenuCardText));
    title->SetFontList(
        title->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
    title->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 6, 0, 0));
    Shrinks(title);
  }

  // The tab or task the keys act on.
  void AddSubject() {
    auto* subject = AddChildView(std::make_unique<views::FlexLayoutView>());
    subject->SetOrientation(views::LayoutOrientation::kHorizontal);
    subject->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
    subject->SetInteriorMargin(gfx::Insets::TLBR(8, kRowInset, 10, kRowInset));
    tabs::TabInterface* tab = window_->GetActiveTabInterface();
    if (!tab || !controller()) {
      return;
    }
    auto* icon = subject->AddChildView(std::make_unique<views::ImageView>());
    icon->SetImageSize(gfx::Size(kIconSize, kIconSize));
    icon->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, 8));
    std::u16string title;
    std::u16string count;
    if (scope_ == KeyScope::kBranch) {
      if (content::NavigationEntry* entry =
              tab->GetContents()->GetController().GetLastCommittedEntry()) {
        icon->SetImage(ui::ImageModel::FromImageSkia(
            entry->GetFavicon().image.AsImageSkia()));
      }
      title = tab->GetContents()->GetTitle();
      const std::optional<PageId> id = controller()->IdFor(tab);
      const size_t under =
          id ? controller()->tree().BranchOf(*id).size() - 1 : 0;
      count = under
                  ? l10n_util::GetStringFUTF16(IDS_UW_KEY_CARD_UNDER_IT,
                                               base::NumberToString16(under))
                  : l10n_util::GetStringUTF16(IDS_UW_KEY_CARD_NOTHING_UNDER_IT);
    } else if (const std::optional<tab_groups::TabGroupId> group =
                   tab->GetGroup()) {
      const TabGroup* task =
          window_->GetTabStripModel()->group_model()->GetTabGroup(*group);
      icon->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kFolderFilledIcon,
          GetTabGroupTabStripColorId(task->visual_data()->color(),
                                     /*active_frame=*/true),
          kIconSize));
      title = task->visual_data()->title();
      count = l10n_util::GetPluralStringFUTF16(IDS_UW_KEY_CARD_TABS,
                                               task->tab_count());
    } else {
      icon->SetImage(ui::ImageModel::FromVectorIcon(
          kListTreeIcon, kColorMenuCardIcon, kIconSize));
      title = l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD);
      const int loose = static_cast<int>(std::ranges::count_if(
          controller()->tree().pages(),
          [](const Page& page) { return page.task.empty(); }));
      count = l10n_util::GetPluralStringFUTF16(IDS_UW_KEY_CARD_TABS, loose);
    }
    Shrinks(subject->AddChildView(MakeText(title, kColorMenuCardText)));
    subject->AddChildView(MakeText(count, kColorMenuCardMuted))
        ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 0, 0));
  }

  std::u16string NameOf(const RowSpec& spec) {
    if (spec.command == Controller::kKeepHere && controller() &&
        !controller()->KeepsTabs(controller()->SelectionFor())) {
      return l10n_util::GetStringUTF16(IDS_UW_STOP_KEEPING_HERE_MAC);
    }
    return GetWindowCommandTitle(window_, spec.command)
        .value_or(l10n_util::GetStringUTF16(spec.name));
  }

  // The caps of a row, or none when its keys are not this scope's.
  std::vector<std::u16string> CapsOf(const RowSpec& spec) {
    const std::optional<ui::Accelerator> first =
        KeyIn(window_, spec.command, scope_);
    const std::optional<ui::Accelerator> second =
        spec.second ? KeyIn(window_, spec.second, scope_) : std::nullopt;
    if (spec.command == kGoToTask1) {
      if (!first || !second) {
        return {};
      }
      return {KeyName(first->key_code()) + u"\u2013" +
              KeyName(second->key_code())};
    }
    std::vector<std::u16string> caps;
    for (const std::optional<ui::Accelerator>& key : {first, second}) {
      if (key) {
        caps.push_back(KeyName(key->key_code()));
      }
    }
    return caps;
  }

  bool Dimmed(const RowSpec& spec) {
    if (spec.command == kGoToTask1) {
      return !CanRun(window_, kGoToTask1);
    }
    return !CanRun(window_, spec.command) &&
           !(spec.second && CanRun(window_, spec.second));
  }

  // `skip_go_to` leaves out the rows that go to a task by number, which the
  // list of tasks takes over.
  void AddRows(base::span<const RowSpec> specs, bool skip_go_to = false) {
    for (const RowSpec& spec : specs) {
      if (skip_go_to &&
          (spec.command == kGoToTask1 || spec.command == kGoToScratchpad)) {
        continue;
      }
      const std::vector<std::u16string> caps = CapsOf(spec);
      if (!caps.empty()) {
        AddChildView(MakeRow(caps, NameOf(spec), Dimmed(spec)));
      }
    }
  }

  void AddSection(int label, base::span<const RowSpec> specs) {
    const size_t before = children().size();
    AddRows(specs);
    if (children().size() > before) {
      AddChildViewAt(MakeSectionLabel(l10n_util::GetStringUTF16(label)),
                     before);
    }
  }

  // With the sidebar hidden there are no numbers on task rows, so the card
  // names the first nine tasks beside their keys.
  void AddTasks() {
    if (!controller()) {
      return;
    }
    const size_t before = children().size();
    const std::vector<std::string> tasks = controller()->tree().Tasks();
    for (size_t i = 0; i < std::min<size_t>(tasks.size(), 9); ++i) {
      const std::optional<ui::Accelerator> key =
          KeyIn(window_, kGoToTask1 + static_cast<int>(i), scope_);
      if (key) {
        AddChildView(MakeRow({KeyName(key->key_code())},
                             controller()->TaskTitle(tasks[i]),
                             /*dimmed=*/false));
      }
    }
    if (const std::optional<ui::Accelerator> key =
            KeyIn(window_, kGoToScratchpad, scope_)) {
      AddChildView(MakeRow({KeyName(key->key_code())},
                           l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD),
                           !CanRun(window_, kGoToScratchpad)));
    }
    if (children().size() > before) {
      AddChildViewAt(
          MakeSectionLabel(l10n_util::GetStringUTF16(IDS_UW_KEY_CARD_GO_TO)),
          before);
    }
  }

  const raw_ptr<BrowserWindowInterface> window_;
  const KeyScope scope_;
};

BEGIN_METADATA(KeyCardView)
END_METADATA

// The card's bubble, 16px into the page card from its top left. It takes no
// clicks and no focus, and deletes itself once its widget is gone.
class KeyCardBubble : public views::BubbleDialogDelegate,
                      public views::WidgetObserver {
 public:
  explicit KeyCardBubble(views::View* page)
      : BubbleDialogDelegate(page, views::BubbleBorder::TOP_LEFT), page_(page) {
    SetCanActivate(false);
    set_focus_traversable_from_anchor_view(false);
  }

  void Watch(views::Widget* widget) { observation_.Observe(widget); }

  // views::BubbleDialogDelegate:
  gfx::Rect GetAnchorRect() const override {
    const gfx::Rect card = page_->GetBoundsInScreen();
    return gfx::Rect(card.x() + kInset, card.y() + kInset, 0, 0);
  }
  void OnBeforeBubbleWidgetInit(views::Widget::InitParams* params,
                                views::Widget* widget) const override {
    params->accept_events = false;
  }

  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    SetAnchorView(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

 private:
  const raw_ptr<views::View> page_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

// Shows and hides the card as the keys go down and up, for the whole app.
class Watcher : public views::WidgetObserver {
 public:
  static Watcher& Get() {
    static base::NoDestructor<Watcher> watcher;
    return *watcher;
  }

  KeyScope ShownIn(const BrowserWindowInterface* window) const {
    return window && window->GetSessionID() == window_ ? shown_
                                                       : KeyScope::kNone;
  }
  base::CallbackListSubscription Observe(base::RepeatingClosure changed) {
    return observers_.Add(std::move(changed));
  }
  views::Widget* widget() { return widget_.get(); }

 private:
  friend class base::NoDestructor<Watcher>;

  Watcher()
      : presses_(ModifierKeys::Get().presses()),
        keys_(ModifierKeys::Get().Observe(
            base::BindRepeating(&Watcher::Changed, base::Unretained(this)))) {}
  ~Watcher() override = default;

  void Changed() {
    const KeyScope held = HeldScope();
    if (ModifierKeys::Get().presses() != presses_) {
      // A key pressed with the scope's keys runs its command, and the card
      // stays away until they are let go.
      presses_ = ModifierKeys::Get().presses();
      waiting_for_release_ = held != KeyScope::kNone;
      Hide();
      return;
    }
    if (held == KeyScope::kNone) {
      waiting_for_release_ = false;
      Hide();
      return;
    }
    if (waiting_for_release_ || held == shown_ || held == armed_) {
      return;
    }
    Hide();
    armed_ = held;
    timer_.Start(FROM_HERE, Delay(),
                 base::BindOnce(&Watcher::Show, base::Unretained(this)));
  }

  void Show() {
    const KeyScope scope = armed_;
    armed_ = KeyScope::kNone;
    BrowserWindowInterface* window =
        GetLastActiveBrowserWindowInterfaceWithAnyProfile();
    BrowserView* view =
        window ? BrowserView::GetBrowserViewForBrowser(window) : nullptr;
    if (!view || !Controller::From(window) || !view->GetWidget() ||
        !view->GetWidget()->IsVisible()) {
      return;
    }
    auto* bubble = new KeyCardBubble(view->contents_container());
    SetUpMenuCard(bubble);
    bubble->set_fixed_width(kCardWidth);
    bubble->set_margins(gfx::Insets::TLBR(12, 12, 10, 12));
    bubble->SetContentsView(std::make_unique<KeyCardView>(window, scope));
    widget_ = views::BubbleDialogDelegate::CreateBubble(bubble);
    bubble->Watch(widget_.get());
    observation_.Observe(widget_.get());
    widget_->ShowInactive();
    shown_ = scope;
    window_ = window->GetSessionID();
    observers_.Notify();
  }

  void Hide() {
    timer_.Stop();
    armed_ = KeyScope::kNone;
    if (shown_ == KeyScope::kNone) {
      return;
    }
    shown_ = KeyScope::kNone;
    window_ = SessionID::InvalidValue();
    observation_.Reset();
    widget_.reset();
    observers_.Notify();
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override {
    // Its window closed under it.
    observation_.Reset();
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(widget_));
    shown_ = KeyScope::kNone;
    window_ = SessionID::InvalidValue();
    observers_.Notify();
  }

  KeyScope armed_ = KeyScope::kNone;
  KeyScope shown_ = KeyScope::kNone;
  SessionID window_ = SessionID::InvalidValue();
  bool waiting_for_release_ = false;
  int presses_ = 0;
  base::OneShotTimer timer_;
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
  base::RepeatingClosureList observers_;
  base::CallbackListSubscription keys_;
};

// The color of the tint: the accent on a light sidebar, and a lighter one on
// a dark sidebar.
SkColor ScopeColor(const views::View* view) {
  const SkColor text = view->GetColorProvider()->GetColor(
      kColorTabForegroundInactiveFrameActive);
  return color_utils::IsDark(text) ? SkColorSetRGB(0x3B, 0x5B, 0xDB)
                                   : SkColorSetRGB(0x9D, 0xB0, 0xFF);
}

BrowserWindowInterface* WindowOf(const views::View* view) {
  const views::Widget* widget = view->GetWidget();
  BrowserView* browser_view = widget
                                  ? BrowserView::GetBrowserViewForNativeWindow(
                                        widget->GetNativeWindow())
                                  : nullptr;
  return browser_view ? browser_view->browser() : nullptr;
}

}  // namespace

// static
void KeyCard::Watch() {
  Watcher::Get();
}

// static
KeyScope KeyCard::ShownIn(const BrowserWindowInterface* window) {
  return Watcher::Get().ShownIn(window);
}

// static
base::CallbackListSubscription KeyCard::Observe(
    base::RepeatingClosure changed) {
  return Watcher::Get().Observe(std::move(changed));
}

// static
views::Widget* KeyCard::WidgetForTesting() {
  return Watcher::Get().widget();
}

// static
void KeyCard::SetDelayForTesting(base::TimeDelta delay) {
  Delay() = delay;
}

SkColor KeyScopeTint(const views::View* row,
                     tabs::TabInterface* tab,
                     std::optional<tab_groups::TabGroupId> task) {
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : WindowOf(row);
  const KeyScope scope = KeyCard::ShownIn(window);
  tabs::TabInterface* selected =
      window ? window->GetActiveTabInterface() : nullptr;
  Controller* controller = window ? Controller::From(window) : nullptr;
  if (scope == KeyScope::kNone || !selected || !controller ||
      (tab && tab->IsPinned())) {
    return SK_ColorTRANSPARENT;
  }
  bool in_scope = false;
  if (scope == KeyScope::kBranch) {
    const std::optional<PageId> top = controller->IdFor(selected);
    const std::optional<PageId> id =
        tab ? controller->IdFor(tab) : std::nullopt;
    in_scope = top && id &&
               std::ranges::contains(controller->tree().BranchOf(*top), *id);
  } else {
    in_scope = tab ? tab->GetGroup() == selected->GetGroup()
                   : task && task == selected->GetGroup();
  }
  return in_scope ? SkColorSetA(ScopeColor(row), kTintAlpha)
                  : SK_ColorTRANSPARENT;
}

TaskKeyNumber::TaskKeyNumber(std::optional<tab_groups::TabGroupId> task)
    : task_(task) {
  SetFontList(font_list().DeriveWithSizeDelta(-2).DeriveWithWeight(
      gfx::Font::Weight::SEMIBOLD));
  SetAutoColorReadabilityEnabled(false);
  SetSubpixelRenderingEnabled(false);
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
  SetPreferredSize(gfx::Size(18, 18));
  SetVisible(false);
  SetCanProcessEventsWithinSubtree(false);
}

TaskKeyNumber::~TaskKeyNumber() = default;

void TaskKeyNumber::AddedToWidget() {
  shown_ = KeyCard::Observe(
      base::BindRepeating(&TaskKeyNumber::Update, base::Unretained(this)));
  if (BrowserWindowInterface* window = WindowOf(this)) {
    if (Controller* controller = Controller::From(window)) {
      tree_ = controller->Observe(
          base::BindRepeating(&TaskKeyNumber::Update, base::Unretained(this)));
    }
  }
  Update();
}

void TaskKeyNumber::RemovedFromWidget() {
  shown_ = {};
  tree_ = {};
}

void TaskKeyNumber::OnThemeChanged() {
  views::Label::OnThemeChanged();
  const SkColor text =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  SetEnabledColor(text);
  SetBackground(views::CreateRoundedRectBackground(SkColorSetA(text, 0x1F), 5));
}

void TaskKeyNumber::Update() {
  BrowserWindowInterface* window = WindowOf(this);
  Controller* controller = window ? Controller::From(window) : nullptr;
  std::optional<int> number;
  if (controller && KeyCard::ShownIn(window) == KeyScope::kTask) {
    if (!task_) {
      number = 0;
    } else {
      const std::vector<std::string> tasks = controller->tree().Tasks();
      for (size_t i = 0; i < std::min<size_t>(tasks.size(), 9); ++i) {
        if (controller->GroupFor(tasks[i]) == task_) {
          number = static_cast<int>(i) + 1;
        }
      }
    }
  }
  if (number) {
    SetText(base::NumberToString16(*number));
  }
  if (GetVisible() != number.has_value()) {
    SetVisible(number.has_value());
    if (parent()) {
      parent()->InvalidateLayout();
    }
  }
}

BEGIN_METADATA(TaskKeyNumber)
END_METADATA

}  // namespace uw
