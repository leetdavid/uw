// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/workspaces/workspace_name_row.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr int kHeight = 28;
constexpr int kInset = 10;
constexpr int kIconSize = 14;
constexpr int kGap = 8;
constexpr int kRingWidth = 2;
constexpr float kFieldRadius = 8;
constexpr gfx::Insets kFieldInsets = gfx::Insets::VH(2, 5);
// The tint's text color at about 78%.
constexpr SkAlpha kNameAlpha = 0xC7;

// The name field. Leaving it saves the name.
class RenameField : public views::Textfield {
  METADATA_HEADER(RenameField, views::Textfield)

 public:
  explicit RenameField(base::RepeatingClosure blurred)
      : blurred_(std::move(blurred)) {}

  // views::Textfield:
  void OnBlur() override {
    Textfield::OnBlur();
    blurred_.Run();
  }

 private:
  base::RepeatingClosure blurred_;
};

BEGIN_METADATA(RenameField)
END_METADATA

const WorkspaceList::Workspace* WorkspaceOf(BrowserWindowInterface* window) {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  return shown ? WorkspaceList::Get().Find(shown->workspace()) : nullptr;
}

}  // namespace

DEFINE_USER_DATA(WorkspaceNameRow);

// static
std::unique_ptr<WorkspaceNameRow> WorkspaceNameRow::Make(
    BrowserWindowInterface* window) {
  return WorkspaceWindow::From(window)
             ? std::make_unique<WorkspaceNameRow>(window)
             : nullptr;
}

// static
WorkspaceNameRow* WorkspaceNameRow::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

WorkspaceNameRow::WorkspaceNameRow(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  layout->SetInteriorMargin(gfx::Insets::VH(0, kInset));
  SetPreferredSize(gfx::Size(0, kHeight));

  const gfx::FontList base;
  icon_ = AddChildView(std::make_unique<views::Label>());
  icon_->SetFontList(base.DeriveWithSizeDelta(kIconSize - base.GetFontSize()));
  icon_->SetElideBehavior(gfx::NO_ELIDE);
  icon_->SetBackgroundColor(SK_ColorTRANSPARENT);
  icon_->SetSubpixelRenderingEnabled(false);
  icon_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, kGap));

  const gfx::FontList bold = base.DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
  name_ = AddChildView(std::make_unique<views::Label>());
  name_->SetFontList(bold);
  name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name_->SetElideBehavior(gfx::ELIDE_TAIL);
  name_->SetBackgroundColor(SK_ColorTRANSPARENT);
  name_->SetSubpixelRenderingEnabled(false);
  name_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  field_ = AddChildView(std::make_unique<RenameField>(base::BindRepeating(
      &WorkspaceNameRow::EndRename, base::Unretained(this), true)));
  field_->SetController(this);
  field_->SetVisible(false);
  field_->SetFontList(bold);
  field_->SetTextColorId(kColorMenuCardText);
  field_->SetBackgroundColor(kColorMenuCardBackground);
  field_->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(kRingWidth, kFieldRadius,
                                     kColorMenuCardAccent),
      kFieldInsets));
  field_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_UW_WORKSPACE_NAME));
  field_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded));

  list_subscription_ = WorkspaceList::Get().Observe(
      base::BindRepeating(&WorkspaceNameRow::Update, base::Unretained(this)));
  window_subscription_ = WorkspaceWindow::From(window_)->ObserveChanges(
      base::BindRepeating(&WorkspaceNameRow::Update, base::Unretained(this)));
  Update();
}

WorkspaceNameRow::~WorkspaceNameRow() = default;

void WorkspaceNameRow::StartRename() {
  if (renaming() || !GetWidget()) {
    return;
  }
  if (BrowserView* view = BrowserView::GetBrowserViewForBrowser(window_)) {
    expand_lock_ = view->tab_strip_view()->GetExpandOnHoverLock(
        ExpandOnHoverLockType::kKeepExpanded);
  }
  views::FocusRing::Remove(field_);
  field_->SetText(std::u16string(name_->GetText()));
  name_->SetVisible(false);
  field_->SetVisible(true);
  field_->SelectAll(/*reversed=*/false);
  field_->RequestFocus();
}

bool WorkspaceNameRow::renaming() const {
  return field_->GetVisible();
}

bool WorkspaceNameRow::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  if (event.GetClickCount() == 2) {
    StartRename();
  }
  return true;
}

void WorkspaceNameRow::OnThemeChanged() {
  views::View::OnThemeChanged();
  name_->SetEnabledColor(SkColorSetA(
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive),
      kNameAlpha));
}

bool WorkspaceNameRow::HandleKeyEvent(views::Textfield* sender,
                                      const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_RETURN:
      EndRename(/*save=*/true);
      return true;
    case ui::VKEY_ESCAPE:
      EndRename(/*save=*/false);
      return true;
    default:
      return false;
  }
}

void WorkspaceNameRow::Update() {
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window_);
  if (!workspace) {
    return;
  }
  icon_->SetText(base::UTF8ToUTF16(workspace->icon));
  name_->SetText(base::UTF8ToUTF16(workspace->name));
}

void WorkspaceNameRow::EndRename(bool save) {
  if (!renaming() || ending_) {
    return;
  }
  base::AutoReset<bool> ending(&ending_, true);
  const std::u16string typed(
      base::TrimWhitespace(field_->GetText(), base::TRIM_ALL));
  if (field_->HasFocus() && GetFocusManager()) {
    GetFocusManager()->ClearFocus();
  }
  field_->SetVisible(false);
  name_->SetVisible(true);
  expand_lock_.reset();
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window_);
  if (save && workspace && !typed.empty()) {
    name_->SetText(typed);
    WorkspaceList::Get().Rename(workspace->id, base::UTF16ToUTF8(typed));
  }
}

BEGIN_METADATA(WorkspaceNameRow)
END_METADATA

}  // namespace uw
