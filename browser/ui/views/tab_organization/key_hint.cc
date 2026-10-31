// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/key_hint.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view_utils.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/branch_toggle.h"
#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

namespace uw {
namespace {

// The close color at 10%, under the rows an Option-click closes.
constexpr SkAlpha kTintAlpha = 0x1A;
// The rows an Option-drag leaves behind.
constexpr float kFaded = 0.5f;

// Set on a part of a row that has a layer only to fade it.
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kFadeLayerKey, false)

// The tab row under the pointer, in whichever window, and whether the
// pointer is on its close button.
struct Pointer {
  tabs::TabHandle tab;
  bool on_close = false;
};

Pointer& Hovered() {
  static Pointer pointer;
  return pointer;
}

// The task SetTaskCloseHint draws, by its window's session, for the pointer
// and for the question.
struct TaskHint {
  SessionID window = SessionID::InvalidValue();
  std::optional<tab_groups::TabGroupId> task;
};
TaskHint& TaskHintFor(bool asking) {
  static TaskHint pointer;
  static TaskHint question;
  return asking ? question : pointer;
}

// The parent an Option-drag moves alone, until the drop.
tabs::TabHandle& DraggedAlone() {
  static tabs::TabHandle tab;
  return tab;
}

// Every row checks again whenever the pointer moves between rows or the
// drag changes.
base::RepeatingClosureList& Rows() {
  static base::NoDestructor<base::RepeatingClosureList> rows;
  return *rows;
}

TabOrganizationController* ControllerOf(tabs::TabInterface* tab) {
  return tab ? TabOrganizationController::From(tab->GetBrowserWindowInterface())
             : nullptr;
}

bool ClosesBranches(tabs::TabInterface* tab) {
  return tab->GetBrowserWindowInterface()->GetProfile()->GetPrefs()->GetBoolean(
      kOptionClickClosesBranchPref);
}

// The hint the pointer and the keys call for: its kind, and the window and
// branch it is about, the branch's top tab first.
struct Hint {
  KeyHint::Shown kind = KeyHint::Shown::kNone;
  raw_ptr<BrowserWindowInterface> window = nullptr;
  std::vector<PageId> branch;
};

Hint Current() {
  tabs::TabInterface* tab = DraggedAlone().Get();
  TabOrganizationController* controller = ControllerOf(tab);
  const bool dragged = controller && controller->dragging_alone();
  if (!dragged) {
    DraggedAlone() = tabs::TabHandle();
    if (!ModifierKeys::Get().option()) {
      return Hint();
    }
    tab = Hovered().tab.Get();
    controller = ControllerOf(tab);
  }
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab) : std::nullopt;
  if (!id) {
    return Hint();
  }
  Hint hint;
  hint.branch = controller->tree().BranchOf(*id);
  if (hint.branch.size() < 2) {
    return Hint();
  }
  if (dragged || !Hovered().on_close) {
    hint.kind = KeyHint::Shown::kStays;
  } else if (ClosesBranches(tab) &&
             controller->CanExecute(TabOrganizationController::kCloseBranch,
                                    {*id})) {
    hint.kind = KeyHint::Shown::kCloses;
  } else {
    return Hint();
  }
  hint.window = tab->GetBrowserWindowInterface();
  return hint;
}

std::u16string TabsUnder(size_t count) {
  return count == 1 ? std::u16string(u"the tab under it")
                    : base::StrCat({u"the ", base::NumberToString16(count),
                                    u" tabs under it"});
}

template <typename T>
T* ChildOfClass(const views::View* row) {
  for (views::View* child : row->children()) {
    if (T* found = views::AsViewClass<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

SkColor CloseHintColor(const views::View* view) {
  const SkColor text = view->GetColorProvider()->GetColor(
      kColorTabForegroundInactiveFrameActive);
  return color_utils::IsDark(text) ? SkColorSetRGB(0xD9, 0x30, 0x25)
                                   : SkColorSetRGB(0xF2, 0x8B, 0x82);
}

KeyHint::KeyHint(tabs::TabHandle tab,
                 views::Button* close,
                 base::RepeatingClosure update_colors)
    : tab_(tab), close_(close), update_colors_(std::move(update_colors)) {
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  close_state_ = close_->AddStateChangedCallback(
      base::BindRepeating(&KeyHint::OnCloseState, base::Unretained(this)));
}

KeyHint::~KeyHint() = default;

void KeyHint::AddedToWidget() {
  keys_ = ModifierKeys::Get().Observe(
      base::BindRepeating(&KeyHint::Update, base::Unretained(this)));
  hints_ =
      Rows().Add(base::BindRepeating(&KeyHint::Update, base::Unretained(this)));
  if (TabOrganizationController* controller = ControllerOf(tab_.Get())) {
    tree_ = controller->Observe(
        base::BindRepeating(&KeyHint::Update, base::Unretained(this)));
    drags_ = controller->ObserveDrags(base::BindRepeating(
        [](KeyHint* hint) {
          // The row under the pointer when an Option-drag begins is the one
          // it moves, and its branch stays faded until the drop.
          TabOrganizationController* controller =
              ControllerOf(hint->tab_.Get());
          if (Hovered().tab == hint->tab_ && controller->dragging_alone()) {
            DraggedAlone() = hint->tab_;
          }
          if (Hovered().tab == hint->tab_ || DraggedAlone() == hint->tab_) {
            Rows().Notify();
          }
        },
        base::Unretained(this)));
  }
  Update();
}

void KeyHint::RemovedFromWidget() {
  keys_ = {};
  hints_ = {};
  tree_ = {};
  drags_ = {};
  if (Hovered().tab == tab_) {
    Hovered() = Pointer();
  }
}

void KeyHint::SetRowHovered(bool hovered) {
  if (hovered) {
    Hovered() = {tab_, close_->GetState() == views::Button::STATE_HOVERED};
  } else if (Hovered().tab == tab_) {
    Hovered() = Pointer();
  } else {
    return;
  }
  Rows().Notify();
}

void KeyHint::OnCloseState() {
  const bool on_close = close_->GetState() == views::Button::STATE_HOVERED ||
                        close_->GetState() == views::Button::STATE_PRESSED;
  if (Hovered().tab != tab_ && !on_close) {
    return;
  }
  Hovered() = {tab_, on_close};
  Rows().Notify();
}

SkColor KeyHint::CloseColor(SkColor color) const {
  return shown_ == Shown::kCloses ? CloseHintColor(this) : color;
}

void KeyHint::OnPaint(gfx::Canvas* canvas) {
  if (shown_ == Shown::kCloses || in_closing_task_) {
    canvas->FillRect(GetLocalBounds(),
                     SkColorSetA(CloseHintColor(this), kTintAlpha));
  }
}

views::View* KeyHint::GetTooltipHandlerForPoint(const gfx::Point& point) {
  return !GetTooltipText().empty() && HitTestPoint(point) ? this : nullptr;
}

void KeyHint::Update() {
  tabs::TabInterface* tab = tab_.Get();
  TabOrganizationController* controller = ControllerOf(tab);
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab) : std::nullopt;
  if (!id) {
    return;
  }
  const Hint hint = Current();
  Shown shown = Shown::kNone;
  bool source = false;
  if (hint.kind != Shown::kNone &&
      hint.window == tab->GetBrowserWindowInterface() &&
      std::ranges::contains(hint.branch, *id)) {
    source = hint.branch.front() == *id;
    shown = hint.kind == Shown::kStays && source ? Shown::kNone : hint.kind;
  }

  // A parent's close button names Option, while the switch is on.
  std::u16string tooltip = u"Close tab";
  const size_t under = controller->tree().BranchOf(*id).size() - 1;
  if (under > 0 && ClosesBranches(tab)) {
    tooltip = shown == Shown::kCloses
                  ? base::StrCat({u"Close this tab and ", TabsUnder(under)})
                  : base::StrCat({u"Close tab\nOption-click to close ",
                                  TabsUnder(under), u" too"});
  }
  if (close_->GetTooltipText() != tooltip) {
    close_->SetTooltipText(tooltip);
  }
  // The parent's row says which tabs an Option-drag leaves behind.
  const bool stays = hint.kind == Shown::kStays && source;
  SetTooltipText(!stays ? std::u16string()
                 : under == 1
                     ? u"Drag to move this tab alone.\nThe tab under it stays."
                     : base::StrCat({u"Drag to move this tab alone.\nThe ",
                                     base::NumberToString16(under),
                                     u" tabs under it stay."}));

  const std::optional<tab_groups::TabGroupId> task = tab->GetGroup();
  const bool in_closing_task =
      task && IsTaskCloseHinted(tab->GetBrowserWindowInterface(), *task);
  if (shown == shown_ && source == source_ &&
      in_closing_task == in_closing_task_) {
    return;
  }
  shown_ = shown;
  source_ = source;
  in_closing_task_ = in_closing_task;
  SetFaded(shown_ == Shown::kStays);
  SchedulePaint();
  update_colors_.Run();
  if (HiddenCount* count = ChildOfClass<HiddenCount>(parent())) {
    count->SetWarning(shown_ == Shown::kCloses && source_);
  }
}

void KeyHint::SetFaded(bool faded) {
  // Chromium's layout animates a row with a layer of the row's own, sets its
  // opacity, and destroys it, so the row's parts fade instead.
  for (views::View* part : parent()->children()) {
    if (part == this) {
      continue;
    }
    if (faded) {
      if (!part->layer()) {
        part->SetPaintToLayer();
        part->layer()->SetFillsBoundsOpaquely(false);
        part->SetProperty(kFadeLayerKey, true);
      }
      part->layer()->SetOpacity(kFaded);
    } else if (part->GetProperty(kFadeLayerKey)) {
      part->ClearProperty(kFadeLayerKey);
      part->DestroyLayer();
    } else if (part->layer()) {
      part->layer()->SetOpacity(1.0f);
    }
  }
}

BEGIN_METADATA(KeyHint)
END_METADATA

void SetTaskCloseHint(bool asking,
                      BrowserWindowInterface* window,
                      const tab_groups::TabGroupId& task,
                      bool on) {
  TaskHint& hint = TaskHintFor(asking);
  const bool shown = hint.task == task && hint.window == window->GetSessionID();
  if (on == shown) {
    return;
  }
  hint = on ? TaskHint{window->GetSessionID(), task} : TaskHint();
  Rows().Notify();
}

bool IsTaskCloseHinted(BrowserWindowInterface* window,
                       const tab_groups::TabGroupId& task) {
  return window && std::ranges::any_of(
                       std::array{false, true}, [window, &task](bool asking) {
                         const TaskHint& hint = TaskHintFor(asking);
                         return hint.task == task &&
                                hint.window == window->GetSessionID();
                       });
}

base::CallbackListSubscription ObserveCloseHints(
    base::RepeatingClosure changed) {
  return Rows().Add(std::move(changed));
}

void SetRowHovered(views::View* row, bool hovered) {
  if (KeyHint* hint = ChildOfClass<KeyHint>(row)) {
    hint->SetRowHovered(hovered);
  }
}

SkColor CloseButtonColor(const views::View* row, SkColor color) {
  const KeyHint* hint = ChildOfClass<KeyHint>(row);
  return hint ? hint->CloseColor(color) : color;
}

}  // namespace uw
